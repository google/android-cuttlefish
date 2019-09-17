/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/strings/str_split.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/size_utils.h"
#include "common/vsoc/lib/vsoc_memory.h"
#include "common/vsoc/shm/screen_layout.h"
#include "host/commands/launch/boot_image_unpacker.h"
#include "host/commands/launch/data_image.h"
#include "host/commands/launch/flags.h"
#include "host/commands/launch/launch.h"
#include "host/commands/launch/launcher_defs.h"
#include "host/commands/launch/process_monitor.h"
#include "host/commands/launch/vsoc_shared_memory.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/commands/kernel_log_monitor/kernel_log_server.h"
#include <host/libs/vm_manager/crosvm_manager.h>
#include "host/libs/vm_manager/vm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

using vsoc::GetPerInstanceDefault;
using cvd::LauncherExitCodes;

namespace {

cvd::OnSocketReadyCb GetOnSubprocessExitCallback(
    const vsoc::CuttlefishConfig& config) {
  if (config.restart_subprocesses()) {
    return cvd::ProcessMonitor::RestartOnExitCb;
  } else {
    return cvd::ProcessMonitor::DoNotMonitorCb;
  }
}

// Maintains the state of the boot process, once a final state is reached
// (success or failure) it sends the appropriate exit code to the foreground
// launcher process
class CvdBootStateMachine {
 public:
  CvdBootStateMachine(cvd::SharedFD fg_launcher_pipe)
      : fg_launcher_pipe_(fg_launcher_pipe), state_(kBootStarted) {}

  // Returns true if the machine is left in a final state
  bool OnBootEvtReceived(cvd::SharedFD boot_events_pipe) {
    monitor::BootEvent evt;
    auto bytes_read = boot_events_pipe->Read(&evt, sizeof(evt));
    if (bytes_read != sizeof(evt)) {
      LOG(ERROR) << "Fail to read a complete event, read " << bytes_read
                 << " bytes only instead of the expected " << sizeof(evt);
      state_ |= kGuestBootFailed;
    } else if (evt == monitor::BootEvent::BootCompleted) {
      LOG(INFO) << "Virtual device booted successfully";
      state_ |= kGuestBootCompleted;
    } else if (evt == monitor::BootEvent::BootFailed) {
      LOG(ERROR) << "Virtual device failed to boot";
      state_ |= kGuestBootFailed;
    }  // Ignore the other signals

    return MaybeWriteToForegroundLauncher();
  }

  bool  OnE2eTestCompleted(int exit_code) {
    if (exit_code != 0) {
      LOG(ERROR) << "VSoC e2e test failed";
      state_ |= kE2eTestFailed;
    } else {
      LOG(INFO) << "VSoC e2e test passed";
      state_ |= kE2eTestPassed;
    }
    return MaybeWriteToForegroundLauncher();
  }

  bool BootCompleted() const {
    bool boot_completed = state_ & kGuestBootCompleted;
    bool test_passed_or_disabled =
        (state_ & kE2eTestPassed) || (state_ & kE2eTestDisabled);
    bool something_failed =
        state_ & ~(kGuestBootCompleted | kE2eTestPassed | kE2eTestDisabled);
    return boot_completed && test_passed_or_disabled && !something_failed;
  }

  bool DisableE2eTests() {
    state_ |= kE2eTestDisabled;
    return MaybeWriteToForegroundLauncher();
  }

  bool BootFailed() const {
    return state_ & (kGuestBootFailed | kE2eTestFailed);
  }

 private:
  void SendExitCode(cvd::LauncherExitCodes exit_code) {
    fg_launcher_pipe_->Write(&exit_code, sizeof(exit_code));
    // The foreground process will exit after receiving the exit code, if we try
    // to write again we'll get a SIGPIPE
    fg_launcher_pipe_->Close();
  }
  bool MaybeWriteToForegroundLauncher() {
    if (fg_launcher_pipe_->IsOpen()) {
      if (BootCompleted()) {
        SendExitCode(cvd::LauncherExitCodes::kSuccess);
      } else if (state_ & kGuestBootFailed) {
        SendExitCode(cvd::LauncherExitCodes::kVirtualDeviceBootFailed);
      } else if (state_ & kE2eTestFailed) {
        SendExitCode(cvd::LauncherExitCodes::kE2eTestFailed);
      } else {
        // No final state was reached
        return false;
      }
    }
    // Either we sent the code before or just sent it, in any case the state is
    // final
    return true;
  }

  cvd::SharedFD fg_launcher_pipe_;
  int state_;
  static const int kBootStarted = 0;
  static const int kGuestBootCompleted = 1 << 0;
  static const int kGuestBootFailed = 1 << 1;
  static const int kE2eTestPassed = 1 << 2;
  static const int kE2eTestFailed = 1 << 3;
  static const int kE2eTestDisabled = 1 << 4;
};

// Abuse the process monitor to make it call us back when boot events are ready
void SetUpHandlingOfBootEvents(
    cvd::ProcessMonitor* process_monitor, cvd::SharedFD boot_events_pipe,
    std::shared_ptr<CvdBootStateMachine> state_machine) {
  process_monitor->MonitorExistingSubprocess(
      // A dummy command, so logs are desciptive
      cvd::Command("boot_events_listener"),
      // A dummy subprocess, with the boot events pipe as control socket
      cvd::Subprocess(-1, boot_events_pipe),
      [boot_events_pipe, state_machine](cvd::MonitorEntry*) {
        auto sent_code = state_machine->OnBootEvtReceived(boot_events_pipe);
        return !sent_code;
      });
}

void LaunchE2eTestIfEnabled(cvd::ProcessMonitor* process_monitor,
                            std::shared_ptr<CvdBootStateMachine> state_machine,
                            const vsoc::CuttlefishConfig& config) {
  if (config.run_e2e_test()) {
    process_monitor->StartSubprocess(
        cvd::Command(config.e2e_test_binary()),
        [state_machine](cvd::MonitorEntry* entry) {
          auto test_result = entry->proc->Wait();
          state_machine->OnE2eTestCompleted(test_result);
          return false;
        });
  } else {
    state_machine->DisableE2eTests();
  }
}

bool WriteCuttlefishEnvironment(const vsoc::CuttlefishConfig& config) {
  auto env = cvd::SharedFD::Open(config.cuttlefish_env_path().c_str(),
                                 O_CREAT | O_RDWR, 0755);
  if (!env->IsOpen()) {
    LOG(ERROR) << "Unable to create cuttlefish.env file";
    return false;
  }
  std::string config_env = "export CUTTLEFISH_PER_INSTANCE_PATH=\"" +
                           config.PerInstancePath(".") + "\"\n";
  config_env += "export ANDROID_SERIAL=";
  if (AdbUsbEnabled(config)) {
    config_env += config.serial_number();
  } else {
    config_env += "127.0.0.1:" + std::to_string(GetHostPort());
  }
  config_env += "\n";
  env->Write(config_env.c_str(), config_env.size());
  return true;
}

// Forks and returns the write end of a pipe to the child process. The parent
// process waits for boot events to come through the pipe and exits accordingly.
cvd::SharedFD DaemonizeLauncher(const vsoc::CuttlefishConfig& config) {
  cvd::SharedFD read_end, write_end;
  if (!cvd::SharedFD::Pipe(&read_end, &write_end)) {
    LOG(ERROR) << "Unable to create pipe";
    return cvd::SharedFD(); // a closed FD
  }
  auto pid = fork();
  if (pid) {
    // Explicitly close here, otherwise we may end up reading forever if the
    // child process dies.
    write_end->Close();
    LauncherExitCodes exit_code;
    auto bytes_read = read_end->Read(&exit_code, sizeof(exit_code));
    if (bytes_read != sizeof(exit_code)) {
      LOG(ERROR) << "Failed to read a complete exit code, read " << bytes_read
                 << " bytes only instead of the expected " << sizeof(exit_code);
      exit_code = LauncherExitCodes::kPipeIOError;
    } else if (exit_code == LauncherExitCodes::kSuccess) {
      LOG(INFO) << "Virtual device booted successfully";
    } else if (exit_code == LauncherExitCodes::kVirtualDeviceBootFailed) {
      LOG(ERROR) << "Virtual device failed to boot";
    } else if (exit_code == LauncherExitCodes::kE2eTestFailed) {
      LOG(ERROR) << "Host VSoC region end to end test failed";
    } else {
      LOG(ERROR) << "Unexpected exit code: " << exit_code;
    }
    std::exit(exit_code);
  } else {
    // The child returns the write end of the pipe
    if (daemon(/*nochdir*/ 1, /*noclose*/ 1) != 0) {
      LOG(ERROR) << "Failed to daemonize child process: " << strerror(errno);
      std::exit(LauncherExitCodes::kDaemonizationError);
    }
    // Redirect standard I/O
    auto log_path = config.launcher_log_path();
    auto log =
        cvd::SharedFD::Open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (!log->IsOpen()) {
      LOG(ERROR) << "Failed to create launcher log file: " << log->StrError();
      std::exit(LauncherExitCodes::kDaemonizationError);
    }
    auto dev_null = cvd::SharedFD::Open("/dev/null", O_RDONLY);
    if (!dev_null->IsOpen()) {
      LOG(ERROR) << "Failed to open /dev/null: " << dev_null->StrError();
      std::exit(LauncherExitCodes::kDaemonizationError);
    }
    if (dev_null->UNMANAGED_Dup2(0) < 0) {
      LOG(ERROR) << "Failed dup2 stdin: " << dev_null->StrError();
      std::exit(LauncherExitCodes::kDaemonizationError);
    }
    if (log->UNMANAGED_Dup2(1) < 0) {
      LOG(ERROR) << "Failed dup2 stdout: " << log->StrError();
      std::exit(LauncherExitCodes::kDaemonizationError);
    }
    if (log->UNMANAGED_Dup2(2) < 0) {
      LOG(ERROR) << "Failed dup2 seterr: " << log->StrError();
      std::exit(LauncherExitCodes::kDaemonizationError);
    }

    read_end->Close();
    return write_end;
  }
}

void ServerLoop(cvd::SharedFD server,
                cvd::ProcessMonitor* process_monitor) {
  while (true) {
    // TODO: use select to handle simultaneous connections.
    auto client = cvd::SharedFD::Accept(*server);
    cvd::LauncherAction action;
    while (client->IsOpen() && client->Read(&action, sizeof(action)) > 0) {
      switch (action) {
        case cvd::LauncherAction::kStop:
          if (process_monitor->StopMonitoredProcesses()) {
            auto response = cvd::LauncherResponse::kSuccess;
            client->Write(&response, sizeof(response));
            std::exit(0);
          } else {
            auto response = cvd::LauncherResponse::kError;
            client->Write(&response, sizeof(response));
          }
          break;
        default:
          LOG(ERROR) << "Unrecognized launcher action: "
                     << static_cast<char>(action);
          auto response = cvd::LauncherResponse::kError;
          client->Write(&response, sizeof(response));
      }
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  auto config = InitFilesystemAndCreateConfig(&argc, &argv);

  // Change working directory to the instance directory as early as possible to
  // ensure all host processes have the same working dir. This helps stop_cvd
  // find the running processes when it can't establish a communication with the
  // launcher.
  auto chdir_ret = chdir(config->instance_dir().c_str());
  if (chdir_ret != 0) {
    auto error = errno;
    LOG(ERROR) << "Unable to change dir into instance directory ("
               << config->instance_dir() << "): " << strerror(error);
    return LauncherExitCodes::kInstanceDirCreationError;
  }

  auto vm_manager = vm_manager::VmManager::Get(config->vm_manager(), config);

  // Check host configuration
  std::vector<std::string> config_commands;
  if (!vm_manager->ValidateHostConfiguration(&config_commands)) {
    LOG(ERROR) << "Validation of user configuration failed";
    std::cout << "Execute the following to correctly configure:" << std::endl;
    for (auto& command : config_commands) {
      std::cout << "  " << command << std::endl;
    }
    std::cout << "You may need to logout for the changes to take effect"
              << std::endl;
    return LauncherExitCodes::kInvalidHostConfiguration;
  }

  if (!WriteCuttlefishEnvironment(*config)) {
    LOG(ERROR) << "Unable to write cuttlefish environment file";
  }

  LOG(INFO) << "The following files contain useful debugging information:";
  if (config->run_as_daemon()) {
    LOG(INFO) << "  Launcher log: " << config->launcher_log_path();
  }
  LOG(INFO) << "  Android's logcat output: " << config->logcat_path();
  LOG(INFO) << "  Kernel log: " << config->PerInstancePath("kernel.log");
  LOG(INFO) << "  Instance configuration: " << GetConfigFilePath(*config);
  LOG(INFO) << "  Instance environment: " << config->cuttlefish_env_path();
  LOG(INFO) << "To access the console run: socat file:$(tty),raw,echo=0 "
            << config->console_path();

  auto launcher_monitor_path = config->launcher_monitor_socket_path();
  auto launcher_monitor_socket = cvd::SharedFD::SocketLocalServer(
      launcher_monitor_path.c_str(), false, SOCK_STREAM, 0666);
  if (!launcher_monitor_socket->IsOpen()) {
    LOG(ERROR) << "Error when opening launcher server: "
               << launcher_monitor_socket->StrError();
    return cvd::LauncherExitCodes::kMonitorCreationFailed;
  }
  cvd::SharedFD foreground_launcher_pipe;
  if (config->run_as_daemon()) {
    foreground_launcher_pipe = DaemonizeLauncher(*config);
    if (!foreground_launcher_pipe->IsOpen()) {
      return LauncherExitCodes::kDaemonizationError;
    }
  } else {
    // Make sure the launcher runs in its own process group even when running in
    // foreground
    if (getsid(0) != getpid()) {
      int retval = setpgid(0, 0);
      if (retval) {
        LOG(ERROR) << "Failed to create new process group: " << strerror(errno);
        std::exit(LauncherExitCodes::kProcessGroupError);
      }
    }
  }

  auto boot_state_machine =
      std::make_shared<CvdBootStateMachine>(foreground_launcher_pipe);

  // Monitor and restart host processes supporting the CVD
  cvd::ProcessMonitor process_monitor;

  auto event_pipes =
      LaunchKernelLogMonitor(*config, &process_monitor, 2);
  cvd::SharedFD boot_events_pipe = event_pipes[0];
  cvd::SharedFD adbd_events_pipe = event_pipes[1];
  event_pipes.clear();

  SetUpHandlingOfBootEvents(&process_monitor, boot_events_pipe,
                            boot_state_machine);

  LaunchLogcatReceiverIfEnabled(*config, &process_monitor);

  LaunchConfigServer(*config, &process_monitor);

  LaunchTombstoneReceiverIfEnabled(*config, &process_monitor);

  LaunchUsbServerIfEnabled(*config, &process_monitor);

  LaunchIvServerIfEnabled(&process_monitor, *config);
  // Launch the e2e tests after the ivserver is ready
  LaunchE2eTestIfEnabled(&process_monitor, boot_state_machine, *config);

  // The vnc server needs to be launched after the ivserver because it connects
  // to it when using qemu. It needs to launch before the VMM because it serves
  // on several sockets (input devices, vsock frame server) when using crosvm.
  auto frontend_enabled = LaunchVNCServerIfEnabled(
      *config, &process_monitor, GetOnSubprocessExitCallback(*config));

  // Start the guest VM
  auto vmm_commands = vm_manager->StartCommands(frontend_enabled);
  for (auto& vmm_cmd: vmm_commands) {
      process_monitor.StartSubprocess(std::move(vmm_cmd),
                                      GetOnSubprocessExitCallback(*config));
  }

  // Start other host processes
  LaunchSocketForwardProxyIfEnabled(&process_monitor, *config);
  LaunchSocketVsockProxyIfEnabled(&process_monitor, *config);
  LaunchStreamAudioIfEnabled(*config, &process_monitor,
                             GetOnSubprocessExitCallback(*config));
  LaunchAdbConnectorIfEnabled(&process_monitor, *config, adbd_events_pipe);

  ServerLoop(launcher_monitor_socket, &process_monitor); // Should not return
  LOG(ERROR) << "The server loop returned, it should never happen!!";
  return cvd::LauncherExitCodes::kServerError;
}
