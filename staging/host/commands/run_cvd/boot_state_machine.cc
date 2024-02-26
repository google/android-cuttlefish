/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/commands/run_cvd/boot_state_machine.h"

#include <poll.h>

#include <memory>
#include <thread>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/kernel_log_monitor/kernel_log_server.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/commands/run_cvd/validate.h"
#include "host/libs/command_util/runner/defs.h"
#include "host/libs/config/feature.h"

DEFINE_int32(reboot_notification_fd, CF_DEFAULTS_REBOOT_NOTIFICATION_FD,
             "A file descriptor to notify when boot completes.");

namespace cuttlefish {
namespace {

// Forks and returns the write end of a pipe to the child process. The parent
// process waits for boot events to come through the pipe and exits accordingly.
SharedFD DaemonizeLauncher(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  auto restore_pipe_name = instance.restore_pipe_name();
  SharedFD read_end, write_end, restore_pipe_read;
  if (!config.snapshot_path().empty()) {
    if (Result<SharedFD> restore_pipe = SharedFD::Fifo(restore_pipe_name, 0600);
        !restore_pipe.ok()) {
      LOG(ERROR) << "Unable to create restore fifo"
                 << restore_pipe.error().FormatForEnv();
      return {};
    } else {
      restore_pipe_read = restore_pipe.value();
    }
  }
  if (!SharedFD::Pipe(&read_end, &write_end)) {
    LOG(ERROR) << "Unable to create pipe";
    return {};  // a closed FD
  }
  auto pid = fork();
  if (pid) {
    // Explicitly close here, otherwise we may end up reading forever if the
    // child process dies.
    write_end->Close();
    RunnerExitCodes exit_code;
    if (!config.snapshot_path().empty()) {
      if (!restore_pipe_read->IsOpen()) {
        LOG(ERROR) << "Error opening restore pipe: "
                   << restore_pipe_read->StrError();
        std::exit(RunnerExitCodes::kDaemonizationError);
      }
      // Try to read from restore pipe. IF successfully reads, that means logcat
      // has started, and the VM has resumed. Exit the thread.
      char buff[1];
      auto read = restore_pipe_read->Read(buff, 1);
      if (read <= 0) {
        LOG(ERROR) << "Could not read restore pipe: "
                   << restore_pipe_read->StrError();
        std::exit(RunnerExitCodes::kDaemonizationError);
      }
      exit_code = RunnerExitCodes::kSuccess;
      std::exit(exit_code);
    }
    auto bytes_read = read_end->Read(&exit_code, sizeof(exit_code));
    if (bytes_read != sizeof(exit_code)) {
      LOG(ERROR) << "Failed to read a complete exit code, read " << bytes_read
                 << " bytes only instead of the expected " << sizeof(exit_code);
      exit_code = RunnerExitCodes::kPipeIOError;
    } else if (exit_code == RunnerExitCodes::kSuccess) {
      LOG(INFO) << "Virtual device booted successfully";
    } else if (exit_code == RunnerExitCodes::kVirtualDeviceBootFailed) {
      LOG(ERROR) << "Virtual device failed to boot";
    } else {
      LOG(ERROR) << "Unexpected exit code: " << exit_code;
    }
    if (exit_code == RunnerExitCodes::kSuccess) {
      LOG(INFO) << kBootCompletedMessage;
    } else {
      LOG(INFO) << kBootFailedMessage;
    }
    std::exit(exit_code);
  } else {
    // The child returns the write end of the pipe
    if (daemon(/*nochdir*/ 1, /*noclose*/ 1) != 0) {
      LOG(ERROR) << "Failed to daemonize child process: " << strerror(errno);
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    // Redirect standard I/O
    auto log_path = instance.launcher_log_path();
    auto log = SharedFD::Open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND,
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (!log->IsOpen()) {
      LOG(ERROR) << "Failed to create launcher log file: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    ::android::base::SetLogger(
        TeeLogger({{LogFileSeverity(), log, MetadataLevel::FULL}}));
    auto dev_null = SharedFD::Open("/dev/null", O_RDONLY);
    if (!dev_null->IsOpen()) {
      LOG(ERROR) << "Failed to open /dev/null: " << dev_null->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    if (dev_null->UNMANAGED_Dup2(0) < 0) {
      LOG(ERROR) << "Failed dup2 stdin: " << dev_null->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    if (log->UNMANAGED_Dup2(1) < 0) {
      LOG(ERROR) << "Failed dup2 stdout: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    if (log->UNMANAGED_Dup2(2) < 0) {
      LOG(ERROR) << "Failed dup2 seterr: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }

    read_end->Close();
    return write_end;
  }
}

Result<SharedFD> ProcessLeader(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSetup<ValidateTapDevices>::Type& /* dependency */) {
  if (!config.snapshot_path().empty()) {
    if (Result<SharedFD> restore_adbd_pipe =
            SharedFD::Fifo(instance.restore_adbd_pipe_name(), 0600);
        !restore_adbd_pipe.ok()) {
      LOG(ERROR) << "Unable to create adbd restore fifo"
                 << restore_adbd_pipe.error().FormatForEnv();
      return {};
    }
  }
  /* These two paths result in pretty different process state, but both
   * achieve the same goal of making the current process the leader of a
   * process group, and are therefore grouped together. */
  if (instance.run_as_daemon()) {
    auto foreground_launcher_pipe = DaemonizeLauncher(config);
    CF_EXPECT(foreground_launcher_pipe->IsOpen());
    return foreground_launcher_pipe;
  }
  // Make sure the launcher runs in its own process group even when running
  // in the foreground
  if (getsid(0) != getpid()) {
    CF_EXPECTF(setpgid(0, 0) == 0, "Failed to create new process group: {}",
               strerror(errno));
  }
  return {};
}

// Maintains the state of the boot process, once a final state is reached
// (success or failure) it sends the appropriate exit code to the foreground
// launcher process
class CvdBootStateMachine : public SetupFeature, public KernelLogPipeConsumer {
 public:
  INJECT(CvdBootStateMachine(AutoSetup<ProcessLeader>::Type& process_leader,
                             KernelLogPipeProvider& kernel_log_pipe_provider))
      : process_leader_(process_leader),
        kernel_log_pipe_provider_(kernel_log_pipe_provider),
        state_(kBootStarted) {}

  ~CvdBootStateMachine() {
    if (interrupt_fd_write_->IsOpen()) {
      char c = 1;
      CHECK_EQ(interrupt_fd_write_->Write(&c, 1), 1)
          << interrupt_fd_write_->StrError();
    }
    if (boot_event_handler_.joinable()) {
      boot_event_handler_.join();
    }
  }

  // SetupFeature
  std::string Name() const override { return "CvdBootStateMachine"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const {
    return {
        static_cast<SetupFeature*>(&process_leader_),
        static_cast<SetupFeature*>(&kernel_log_pipe_provider_),
    };
  }
  Result<void> ResultSetup() override {
    CF_EXPECT(SharedFD::Pipe(&interrupt_fd_read_, &interrupt_fd_write_));
    CF_EXPECT(interrupt_fd_read_->IsOpen(), interrupt_fd_read_->StrError());
    CF_EXPECT(interrupt_fd_write_->IsOpen(), interrupt_fd_write_->StrError());
    fg_launcher_pipe_ = *process_leader_;
    if (FLAGS_reboot_notification_fd >= 0) {
      reboot_notification_ = SharedFD::Dup(FLAGS_reboot_notification_fd);
      CF_EXPECTF(reboot_notification_->IsOpen(),
                 "Could not dup fd given for reboot_notification_fd: {}",
                 reboot_notification_->StrError());
      close(FLAGS_reboot_notification_fd);
    }
    SharedFD boot_events_pipe = kernel_log_pipe_provider_.KernelLogPipe();
    CF_EXPECTF(boot_events_pipe->IsOpen(), "Could not get boot events pipe: {}",
               boot_events_pipe->StrError());
    boot_event_handler_ = std::thread(
        [this, boot_events_pipe]() { ThreadLoop(boot_events_pipe); });
    return {};
  }

  void ThreadLoop(SharedFD boot_events_pipe) {
    while (true) {
      std::vector<PollSharedFd> poll_shared_fd = {
          {
              .fd = boot_events_pipe,
              .events = POLLIN | POLLHUP,
          },
          {
              .fd = interrupt_fd_read_,
              .events = POLLIN | POLLHUP,
          }};
      int result = SharedFD::Poll(poll_shared_fd, -1);
      if (poll_shared_fd[1].revents & POLLIN) {
        return;
      }
      if (result < 0) {
        PLOG(FATAL) << "Failed to call Select";
        return;
      }
      if (poll_shared_fd[0].revents & POLLHUP) {
        LOG(ERROR) << "Failed to read a complete kernel log boot event.";
        state_ |= kGuestBootFailed;
        if (MaybeWriteNotification()) {
          break;
        }
      }
      if (!(poll_shared_fd[0].revents & POLLIN)) {
        continue;
      }
      auto sent_code = OnBootEvtReceived(boot_events_pipe);
      if (sent_code) {
        break;
      }
    }
  }

  // Returns true if the machine is left in a final state
  bool OnBootEvtReceived(SharedFD boot_events_pipe) {
    std::optional<monitor::ReadEventResult> read_result =
        monitor::ReadEvent(boot_events_pipe);
    if (!read_result) {
      LOG(ERROR) << "Failed to read a complete kernel log boot event.";
      state_ |= kGuestBootFailed;
      return MaybeWriteNotification();
    }

    if (read_result->event == monitor::Event::BootCompleted) {
      LOG(INFO) << "Virtual device booted successfully";
      state_ |= kGuestBootCompleted;
    } else if (read_result->event == monitor::Event::BootFailed) {
      LOG(ERROR) << "Virtual device failed to boot";
      state_ |= kGuestBootFailed;
    }  // Ignore the other signals

    return MaybeWriteNotification();
  }
  bool BootCompleted() const { return state_ & kGuestBootCompleted; }
  bool BootFailed() const { return state_ & kGuestBootFailed; }

  void SendExitCode(RunnerExitCodes exit_code, SharedFD fd) {
    fd->Write(&exit_code, sizeof(exit_code));
    // The foreground process will exit after receiving the exit code, if we try
    // to write again we'll get a SIGPIPE
    fd->Close();
  }
  bool MaybeWriteNotification() {
    std::vector<SharedFD> fds = {reboot_notification_, fg_launcher_pipe_};
    for (auto& fd : fds) {
      if (fd->IsOpen()) {
        if (BootCompleted()) {
          SendExitCode(RunnerExitCodes::kSuccess, fd);
        } else if (state_ & kGuestBootFailed) {
          SendExitCode(RunnerExitCodes::kVirtualDeviceBootFailed, fd);
        }
      }
    }
    // Either we sent the code before or just sent it, in any case the state is
    // final
    return BootCompleted() || (state_ & kGuestBootFailed);
  }

  AutoSetup<ProcessLeader>::Type& process_leader_;
  KernelLogPipeProvider& kernel_log_pipe_provider_;

  std::thread boot_event_handler_;
  SharedFD fg_launcher_pipe_;
  SharedFD reboot_notification_;
  SharedFD interrupt_fd_read_;
  SharedFD interrupt_fd_write_;
  int state_;
  static const int kBootStarted = 0;
  static const int kGuestBootCompleted = 1 << 0;
  static const int kGuestBootFailed = 1 << 1;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig, KernelLogPipeProvider,
                                 const CuttlefishConfig::InstanceSpecific,
                                 AutoSetup<ValidateTapDevices>::Type>>
bootStateMachineComponent() {
  return fruit::createComponent()
      .addMultibinding<KernelLogPipeConsumer, CvdBootStateMachine>()
      .addMultibinding<SetupFeature, CvdBootStateMachine>()
      .install(AutoSetup<ProcessLeader>::Component);
}

}  // namespace cuttlefish
