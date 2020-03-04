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

#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/fs/tee.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/size_utils.h"
#include "host/commands/run_cvd/kernel_args.h"
#include "host/commands/run_cvd/launch.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/commands/kernel_log_monitor/kernel_log_server.h"
#include <host/libs/vm_manager/crosvm_manager.h>
#include "host/libs/vm_manager/vm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

using vsoc::ForCurrentInstance;
using cvd::RunnerExitCodes;

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

  bool BootCompleted() const {
    return state_ & kGuestBootCompleted;
  }

  bool BootFailed() const {
    return state_ & kGuestBootFailed;
  }

 private:
  void SendExitCode(cvd::RunnerExitCodes exit_code) {
    fg_launcher_pipe_->Write(&exit_code, sizeof(exit_code));
    // The foreground process will exit after receiving the exit code, if we try
    // to write again we'll get a SIGPIPE
    fg_launcher_pipe_->Close();
  }
  bool MaybeWriteToForegroundLauncher() {
    if (fg_launcher_pipe_->IsOpen()) {
      if (BootCompleted()) {
        SendExitCode(cvd::RunnerExitCodes::kSuccess);
      } else if (state_ & kGuestBootFailed) {
        SendExitCode(cvd::RunnerExitCodes::kVirtualDeviceBootFailed);
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

bool WriteCuttlefishEnvironment(const vsoc::CuttlefishConfig& config) {
  auto env = cvd::SharedFD::Open(config.cuttlefish_env_path().c_str(),
                                 O_CREAT | O_RDWR, 0755);
  if (!env->IsOpen()) {
    LOG(ERROR) << "Unable to create cuttlefish.env file";
    return false;
  }
  auto instance = config.ForDefaultInstance();
  std::string config_env = "export CUTTLEFISH_PER_INSTANCE_PATH=\"" +
                           instance.PerInstancePath(".") + "\"\n";
  config_env += "export ANDROID_SERIAL=" + instance.adb_ip_and_port() + "\n";
  env->Write(config_env.c_str(), config_env.size());
  return true;
}

// Forks and returns the write end of a pipe to the child process. The parent
// process waits for boot events to come through the pipe and exits accordingly.
cvd::SharedFD DaemonizeLauncher(const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
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
    RunnerExitCodes exit_code;
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
      LOG(INFO) << vsoc::kBootCompletedMessage;
    } else {
      LOG(INFO) << vsoc::kBootFailedMessage;
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
    auto log =
        cvd::SharedFD::Open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (!log->IsOpen()) {
      LOG(ERROR) << "Failed to create launcher log file: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    auto dev_null = cvd::SharedFD::Open("/dev/null", O_RDONLY);
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
        case cvd::LauncherAction::kStatus: {
          // TODO(schuffelen): Return more information on a side channel
          auto response = cvd::LauncherResponse::kSuccess;
          client->Write(&response, sizeof(response));
          break;
        }
        default:
          LOG(ERROR) << "Unrecognized launcher action: "
                     << static_cast<char>(action);
          auto response = cvd::LauncherResponse::kError;
          client->Write(&response, sizeof(response));
      }
    }
  }
}

std::string GetConfigFilePath(const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.PerInstancePath("cuttlefish_config.json");
}

}  // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  if (isatty(0)) {
    LOG(FATAL) << "stdin was a tty, expected to be passed the output of a previous stage. "
               << "Did you mean to run launch_cvd?";
    return cvd::RunnerExitCodes::kInvalidHostConfiguration;
  } else {
    int error_num = errno;
    if (error_num == EBADF) {
      LOG(FATAL) << "stdin was not a valid file descriptor, expected to be passed the output "
                 << "of assemble_cvd. Did you mean to run launch_cvd?";
      return cvd::RunnerExitCodes::kInvalidHostConfiguration;
    }
  }

  cvd::TeeStderrToFile stderr_tee;

  std::string input_files_str;
  {
    auto input_fd = cvd::SharedFD::Dup(0);
    auto bytes_read = cvd::ReadAll(input_fd, &input_files_str);
    if (bytes_read < 0) {
      LOG(FATAL) << "Failed to read input files. Error was \"" << input_fd->StrError() << "\"";
    }
  }
  std::vector<std::string> input_files = android::base::Split(input_files_str, "\n");
  bool found_config = false;
  for (const auto& file : input_files) {
    if (file.find("cuttlefish_config.json") != std::string::npos) {
      found_config = true;
      setenv(vsoc::kCuttlefishConfigEnvVarName, file.c_str(), /* overwrite */ false);
    }
  }
  if (!found_config) {
    return RunnerExitCodes::kCuttlefishConfigurationInitError;
  }

  auto config = vsoc::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();

  auto runner_log_path = instance.PerInstancePath("run_cvd.log");
  stderr_tee.SetFile(cvd::SharedFD::Creat(runner_log_path.c_str(), 0755));

  // Change working directory to the instance directory as early as possible to
  // ensure all host processes have the same working dir. This helps stop_cvd
  // find the running processes when it can't establish a communication with the
  // launcher.
  auto chdir_ret = chdir(instance.instance_dir().c_str());
  if (chdir_ret != 0) {
    auto error = errno;
    LOG(ERROR) << "Unable to change dir into instance directory ("
               << instance.instance_dir() << "): " << strerror(error);
    return RunnerExitCodes::kInstanceDirCreationError;
  }

  auto used_tap_devices = cvd::TapInterfacesInUse();
  if (used_tap_devices.count(instance.wifi_tap_name())) {
    LOG(ERROR) << "Wifi TAP device already in use";
    return RunnerExitCodes::kTapDeviceInUse;
  } else if (used_tap_devices.count(instance.mobile_tap_name())) {
    LOG(ERROR) << "Mobile TAP device already in use";
    return RunnerExitCodes::kTapDeviceInUse;
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
    return RunnerExitCodes::kInvalidHostConfiguration;
  }

  if (!WriteCuttlefishEnvironment(*config)) {
    LOG(ERROR) << "Unable to write cuttlefish environment file";
  }

  LOG(INFO) << "The following files contain useful debugging information:";
  if (config->run_as_daemon()) {
    LOG(INFO) << "  Launcher log: " << instance.launcher_log_path();
  }
  LOG(INFO) << "  Android's logcat output: " << instance.logcat_path();
  LOG(INFO) << "  Kernel log: " << instance.PerInstancePath("kernel.log");
  LOG(INFO) << "  Instance configuration: " << GetConfigFilePath(*config);
  LOG(INFO) << "  Instance environment: " << config->cuttlefish_env_path();
  LOG(INFO) << "To access the console run: socat file:$(tty),raw,echo=0 "
            << instance.console_path();

  auto launcher_monitor_path = instance.launcher_monitor_socket_path();
  auto launcher_monitor_socket = cvd::SharedFD::SocketLocalServer(
      launcher_monitor_path.c_str(), false, SOCK_STREAM, 0666);
  if (!launcher_monitor_socket->IsOpen()) {
    LOG(ERROR) << "Error when opening launcher server: "
               << launcher_monitor_socket->StrError();
    return cvd::RunnerExitCodes::kMonitorCreationFailed;
  }
  cvd::SharedFD foreground_launcher_pipe;
  if (config->run_as_daemon()) {
    foreground_launcher_pipe = DaemonizeLauncher(*config);
    if (!foreground_launcher_pipe->IsOpen()) {
      return RunnerExitCodes::kDaemonizationError;
    }
  } else {
    // Make sure the launcher runs in its own process group even when running in
    // foreground
    if (getsid(0) != getpid()) {
      int retval = setpgid(0, 0);
      if (retval) {
        LOG(ERROR) << "Failed to create new process group: " << strerror(errno);
        std::exit(RunnerExitCodes::kProcessGroupError);
      }
    }
  }

  auto boot_state_machine =
      std::make_shared<CvdBootStateMachine>(foreground_launcher_pipe);

  // Monitor and restart host processes supporting the CVD
  cvd::ProcessMonitor process_monitor;

  if (config->enable_metrics() == vsoc::CuttlefishConfig::kYes) {
    LaunchMetrics(&process_monitor, *config);
  }

  auto event_pipes =
      LaunchKernelLogMonitor(*config, &process_monitor, 2);
  cvd::SharedFD boot_events_pipe = event_pipes[0];
  cvd::SharedFD adbd_events_pipe = event_pipes[1];
  event_pipes.clear();

  std::set<std::string> extra_kernel_cmdline;

  SetUpHandlingOfBootEvents(&process_monitor, boot_events_pipe,
                            boot_state_machine);

  auto logcat_server = LaunchLogcatReceiverIfEnabled(*config, &process_monitor);
  auto logcat_server_args = KernelCommandLineFromLogcatServer(logcat_server);

  auto config_server = LaunchConfigServer(*config, &process_monitor);
  auto config_server_args = KernelCommandLineFromConfigServer(config_server);

  auto tombstone_server = LaunchTombstoneReceiverIfEnabled(*config, &process_monitor);
  auto tombstone_kernel_args = KernelCommandLineFromTombstone(tombstone_server);

  auto tpm_server = LaunchTpm(&process_monitor, *config);
  auto tpm_kernel_args = KernelCommandLineFromTpm(tpm_server);

  // The streamer needs to launch before the VMM because it serves on several
  // sockets (input devices, vsock frame server) when using crosvm.
  StreamerLaunchResult streamer_config;
  if (config->enable_vnc_server()) {
    streamer_config = LaunchVNCServer(
      *config, &process_monitor, GetOnSubprocessExitCallback(*config));
  }
  if (config->enable_webrtc()) {
    streamer_config = LaunchWebRTC(&process_monitor, *config);
  }

  auto streamer_kernel_args = KernelCommandLineFromStreamer(streamer_config);

  auto kernel_args = KernelCommandLineFromConfig(*config);
  kernel_args.insert(kernel_args.end(), streamer_kernel_args.begin(),
                     streamer_kernel_args.end());
  kernel_args.insert(kernel_args.end(), tombstone_kernel_args.begin(),
                     tombstone_kernel_args.end());
  kernel_args.insert(kernel_args.end(), config_server_args.begin(), config_server_args.end());
  kernel_args.insert(kernel_args.end(), logcat_server_args.begin(), logcat_server_args.end());
  kernel_args.insert(kernel_args.end(), tpm_kernel_args.begin(), tpm_kernel_args.end());

  // Start the guest VM
  vm_manager->WithFrontend(streamer_config.launched);
  vm_manager->WithKernelCommandLine(android::base::Join(kernel_args, " "));
  auto vmm_commands = vm_manager->StartCommands();
  for (auto& vmm_cmd: vmm_commands) {
      process_monitor.StartSubprocess(std::move(vmm_cmd),
                                      GetOnSubprocessExitCallback(*config));
  }

  // Start other host processes
  LaunchSocketVsockProxyIfEnabled(&process_monitor, *config);
  LaunchAdbConnectorIfEnabled(&process_monitor, *config, adbd_events_pipe);

  ServerLoop(launcher_monitor_socket, &process_monitor); // Should not return
  LOG(ERROR) << "The server loop returned, it should never happen!!";
  return cvd::RunnerExitCodes::kServerError;
}
