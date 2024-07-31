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

#include <android-base/file.h>
#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include "common/libs/utils/result.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/kernel_log_monitor/kernel_log_server.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/commands/run_cvd/validate.h"
#include "host/libs/command_util/runner/defs.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/feature.h"
#include "openwrt_control.grpc.pb.h"

using grpc::ClientContext;
using openwrtcontrolserver::LuciRpcReply;
using openwrtcontrolserver::LuciRpcRequest;
using openwrtcontrolserver::OpenwrtControlService;
using openwrtcontrolserver::OpenwrtIpaddrReply;

DEFINE_int32(reboot_notification_fd, CF_DEFAULTS_REBOOT_NOTIFICATION_FD,
             "A file descriptor to notify when boot completes.");

namespace cuttlefish {
namespace {

// Forks run_cvd into a daemonized child process. The current process continues
// only until the child has signalled that the boot is finished.
//
// `DaemonizeLauncher` returns the write end of a pipe. The child is expected
// to write a `RunnerExitCodes` into the pipe when the boot finishes.
Result<SharedFD> DaemonizeLauncher(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  SharedFD read_end, write_end;
  CF_EXPECT(SharedFD::Pipe(&read_end, &write_end), "Unable to create pipe");
  auto pid = fork();
  if (pid) {
    // Explicitly close here, otherwise we may end up reading forever if the
    // child process dies.
    write_end->Close();
    RunnerExitCodes exit_code;
    auto bytes_read = read_end->Read(&exit_code, sizeof(exit_code));
    if (bytes_read != sizeof(exit_code)) {
      LOG(ERROR) << "Failed to read a complete exit code, read " << bytes_read
                 << " bytes only instead of the expected " << sizeof(exit_code);
      exit_code = RunnerExitCodes::kPipeIOError;
    } else if (exit_code == RunnerExitCodes::kSuccess) {
      if (IsRestoring(config)) {
        LOG(INFO) << "Virtual device restored successfully";
      } else {
        LOG(INFO) << "Virtual device booted successfully";
      }
    } else if (exit_code == RunnerExitCodes::kVirtualDeviceBootFailed) {
      if (IsRestoring(config)) {
        LOG(ERROR) << "Virtual device failed to restore";
      } else {
        LOG(ERROR) << "Virtual device failed to boot";
      }
      if (!instance.fail_fast()) {
        LOG(ERROR) << "Device has been left running for debug";
      }
    } else {
      LOG(ERROR) << "Unexpected exit code: " << exit_code;
    }
    if (!IsRestoring(config)) {
      if (exit_code == RunnerExitCodes::kSuccess) {
        LOG(INFO) << kBootCompletedMessage;
      } else {
        LOG(INFO) << kBootFailedMessage;
      }
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
  if (IsRestoring(config)) {
    CF_EXPECT(SharedFD::Fifo(instance.restore_adbd_pipe_name(), 0600),
              "Unable to create adbd restore fifo");
  }
  /* These two paths result in pretty different process state, but both
   * achieve the same goal of making the current process the leader of a
   * process group, and are therefore grouped together. */
  if (instance.run_as_daemon()) {
    return CF_EXPECT(DaemonizeLauncher(config), "DaemonizeLauncher failed");
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
  INJECT(
      CvdBootStateMachine(const CuttlefishConfig& config,
                          AutoSetup<ProcessLeader>::Type& process_leader,
                          KernelLogPipeProvider& kernel_log_pipe_provider,
                          const vm_manager::VmManager& vm_manager,
                          const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config),
        process_leader_(process_leader),
        kernel_log_pipe_provider_(kernel_log_pipe_provider),
        vm_manager_(vm_manager),
        instance_(instance),
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
    if (restore_complete_stop_write_->IsOpen()) {
      char c = 1;
      CHECK_EQ(restore_complete_stop_write_->Write(&c, 1), 1)
          << restore_complete_stop_write_->StrError();
    }
    if (restore_complete_handler_.joinable()) {
      restore_complete_handler_.join();
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

    // Pipe to tell `ThreadLoop` that the restore is complete.
    SharedFD restore_complete_pipe, restore_complete_pipe_write;
    // Pipe to tell `restore_complete_handler_` thread to give up.
    // It isn't perfect, can only break out of the `WaitForRestoreComplete`
    // step.
    SharedFD restore_complete_stop_read;
    if (IsRestoring(config_)) {
      CF_EXPECT(
          SharedFD::Pipe(&restore_complete_pipe, &restore_complete_pipe_write),
          "unable to create pipe");
      CF_EXPECT(SharedFD::Pipe(&restore_complete_stop_read,
                               &restore_complete_stop_write_),
                "unable to create pipe");

      restore_complete_handler_ = std::thread(
          [this, restore_complete_pipe_write, restore_complete_stop_read]() {
            const auto result =
                vm_manager_.WaitForRestoreComplete(restore_complete_stop_read);
            CHECK(result.ok()) << "Failed to wait for restore complete: "
                               << result.error().FormatForEnv();
            if (!result.value()) {
              return;
            }

            cuttlefish::SharedFD restore_adbd_pipe = cuttlefish::SharedFD::Open(
                config_.ForDefaultInstance().restore_adbd_pipe_name().c_str(),
                O_WRONLY);
            CHECK(restore_adbd_pipe->IsOpen())
                << "Error opening adbd restore pipe: "
                << restore_adbd_pipe->StrError();
            CHECK(cuttlefish::WriteAll(restore_adbd_pipe, "2") == 1)
                << "Error writing to adbd restore pipe: "
                << restore_adbd_pipe->StrError() << ". This is unrecoverable.";

            // Restart network service in OpenWRT, broken on restore.
            CHECK(FileExists(instance_.grpc_socket_path() +
                             "/OpenwrtControlServer.sock"))
                << "unable to find grpc socket for OpenwrtControlServer";
            auto openwrt_channel =
                grpc::CreateChannel("unix:" + instance_.grpc_socket_path() +
                                        "/OpenwrtControlServer.sock",
                                    grpc::InsecureChannelCredentials());
            auto stub_ = OpenwrtControlService::NewStub(openwrt_channel);
            LuciRpcRequest request;
            request.set_subpath("sys");
            request.set_method("exec");
            request.add_params("service network restart");
            LuciRpcReply response;
            ClientContext context;
            grpc::Status status = stub_->LuciRpc(&context, request, &response);
            CHECK(status.ok())
                << "Failed to send network service reset" << status.error_code()
                << ": " << status.error_message();
            LOG(DEBUG) << "OpenWRT `service network restart` response: "
                       << response.result();

            auto SubtoolPath = [](const std::string& subtool_name) {
              auto my_own_dir = android::base::GetExecutableDirectory();
              std::stringstream subtool_path_stream;
              subtool_path_stream << my_own_dir << "/" << subtool_name;
              auto subtool_path = subtool_path_stream.str();
              if (my_own_dir.empty() || !FileExists(subtool_path)) {
                return HostBinaryPath(subtool_name);
              }
              return subtool_path;
            };
            // Run the in-guest post-restore script.
            Command adb_command(SubtoolPath("adb"));
            // Avoid the adb server being started in the runtime directory and
            // looking like a process that is still using the directory.
            adb_command.SetWorkingDirectory("/");
            adb_command.AddParameter("-s").AddParameter(
                instance_.adb_ip_and_port());
            adb_command.AddParameter("wait-for-device");
            adb_command.AddParameter("shell");
            adb_command.AddParameter("/vendor/bin/snapshot_hook_post_resume");
            CHECK_EQ(adb_command.Start().Wait(), 0)
                << "Failed to run /vendor/bin/snapshot_hook_post_resume";
            // Done last so that adb is more likely to be ready.
            CHECK(cuttlefish::WriteAll(restore_complete_pipe_write, "1") == 1)
                << "Error writing to restore complete pipe: "
                << restore_complete_pipe_write->StrError()
                << ". This is unrecoverable.";
          });
    }

    boot_event_handler_ =
        std::thread([this, boot_events_pipe, restore_complete_pipe]() {
          ThreadLoop(boot_events_pipe, restore_complete_pipe);
        });

    return {};
  }

  void ThreadLoop(SharedFD boot_events_pipe, SharedFD restore_complete_pipe) {
    while (true) {
      std::vector<PollSharedFd> poll_shared_fd = {
          {
              .fd = boot_events_pipe,
              .events = POLLIN | POLLHUP,
          },
          {
              .fd = restore_complete_pipe,
              .events = restore_complete_pipe->IsOpen()
                            ? (short)(POLLIN | POLLHUP)
                            : (short)0,
          },
          {
              .fd = interrupt_fd_read_,
              .events = POLLIN | POLLHUP,
          },
      };
      int result = SharedFD::Poll(poll_shared_fd, -1);
      // interrupt_fd_read_
      if (poll_shared_fd[2].revents & POLLIN) {
        return;
      }
      if (result < 0) {
        PLOG(FATAL) << "Failed to call Select";
        return;
      }
      // boot_events_pipe
      if (poll_shared_fd[0].revents & POLLHUP) {
        LOG(ERROR) << "Failed to read a complete kernel log boot event.";
        state_ |= kGuestBootFailed;
        if (MaybeWriteNotification()) {
          break;
        }
      }
      if (poll_shared_fd[0].revents & POLLIN) {
        auto sent_code = OnBootEvtReceived(boot_events_pipe);
        if (sent_code) {
          if (!BootCompleted()) {
            if (!instance_.fail_fast()) {
              LOG(ERROR) << "Device running, likely in a bad state";
              break;
            }
            auto monitor_res = GetLauncherMonitorFromInstance(instance_, 5);
            CHECK(monitor_res.ok()) << monitor_res.error().FormatForEnv();
            auto fail_res = RunLauncherAction(
                *monitor_res, LauncherAction::kFail, std::optional<int>());
            CHECK(fail_res.ok()) << fail_res.error().FormatForEnv();
          }
          break;
        }
      }
      // restore_complete_pipe
      if (poll_shared_fd[1].revents & POLLIN) {
        char buff[1];
        auto read = restore_complete_pipe->Read(buff, 1);
        if (read <= 0) {
          LOG(ERROR) << "Could not read restore pipe: "
                     << restore_complete_pipe->StrError();
          state_ |= kGuestBootFailed;
          if (MaybeWriteNotification()) {
            break;
          }
        }
        state_ |= kGuestBootCompleted;
        if (MaybeWriteNotification()) {
          break;
        }
      }
      if (poll_shared_fd[1].revents & POLLHUP) {
        LOG(ERROR) << "restore_complete_pipe closed unexpectedly";
        state_ |= kGuestBootFailed;
        if (MaybeWriteNotification()) {
          break;
        }
      }
    }
  }

  // Returns true if the machine is left in a final state
  bool OnBootEvtReceived(SharedFD boot_events_pipe) {
    Result<std::optional<monitor::ReadEventResult>> read_result =
        monitor::ReadEvent(boot_events_pipe);
    if (!read_result) {
      LOG(ERROR) << "Failed to read a complete kernel log boot event: "
                 << read_result.error().FormatForEnv();
      state_ |= kGuestBootFailed;
      return MaybeWriteNotification();
    } else if (!*read_result) {
      LOG(ERROR) << "EOF from kernel log monitor";
      state_ |= kGuestBootFailed;
      return MaybeWriteNotification();
    }

    if ((*read_result)->event == monitor::Event::BootCompleted) {
      LOG(INFO) << "Virtual device booted successfully";
      state_ |= kGuestBootCompleted;
    } else if ((*read_result)->event == monitor::Event::BootFailed) {
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

  const CuttlefishConfig& config_;
  AutoSetup<ProcessLeader>::Type& process_leader_;
  KernelLogPipeProvider& kernel_log_pipe_provider_;
  const vm_manager::VmManager& vm_manager_;
  const CuttlefishConfig::InstanceSpecific& instance_;

  std::thread boot_event_handler_;
  std::thread restore_complete_handler_;
  SharedFD restore_complete_stop_write_;
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
                                 const vm_manager::VmManager,
                                 AutoSetup<ValidateTapDevices>::Type>>
bootStateMachineComponent() {
  return fruit::createComponent()
      .addMultibinding<KernelLogPipeConsumer, CvdBootStateMachine>()
      .addMultibinding<SetupFeature, CvdBootStateMachine>()
      .install(AutoSetup<ProcessLeader>::Component);
}

}  // namespace cuttlefish
