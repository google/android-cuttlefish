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

#include <unistd.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/run_cvd/boot_state_machine.h"
#include "host/commands/run_cvd/launch.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/server_loop.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/host_configuration.h"
#include "host/libs/vm_manager/vm_manager.h"

DEFINE_int32(reboot_notification_fd, -1,
             "A file descriptor to notify when boot completes.");

namespace cuttlefish {

using vm_manager::GetVmManager;
using vm_manager::ValidateHostConfiguration;

namespace {

constexpr char kGreenColor[] = "\033[1;32m";
constexpr char kResetColor[] = "\033[0m";

bool WriteCuttlefishEnvironment(const CuttlefishConfig& config) {
  auto env = SharedFD::Open(config.cuttlefish_env_path().c_str(),
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
SharedFD DaemonizeLauncher(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  SharedFD read_end, write_end;
  if (!SharedFD::Pipe(&read_end, &write_end)) {
    LOG(ERROR) << "Unable to create pipe";
    return {}; // a closed FD
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

std::string GetConfigFilePath(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.PerInstancePath("cuttlefish_config.json");
}

void PrintStreamingInformation(const CuttlefishConfig& config) {
  if (config.ForDefaultInstance().start_webrtc_sig_server()) {
    // TODO (jemoreira): Change this when webrtc is moved to the debian package.
    LOG(INFO) << kGreenColor << "Point your browser to https://"
              << config.sig_server_address() << ":" << config.sig_server_port()
              << " to interact with the device." << kResetColor;
  } else if (config.enable_vnc_server()) {
    LOG(INFO) << kGreenColor << "VNC server started on port "
              << config.ForDefaultInstance().vnc_server_port() << kResetColor;
  }
  // When WebRTC is enabled but an operator other than the one launched by
  // run_cvd is used there is no way to know the url to which to point the
  // browser to.
}

}  // namespace

int RunCvdMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  if (isatty(0)) {
    LOG(FATAL) << "stdin was a tty, expected to be passed the output of a previous stage. "
               << "Did you mean to run launch_cvd?";
    return RunnerExitCodes::kInvalidHostConfiguration;
  } else {
    int error_num = errno;
    if (error_num == EBADF) {
      LOG(FATAL) << "stdin was not a valid file descriptor, expected to be passed the output "
                 << "of assemble_cvd. Did you mean to run launch_cvd?";
      return RunnerExitCodes::kInvalidHostConfiguration;
    }
  }

  std::string input_files_str;
  {
    auto input_fd = SharedFD::Dup(0);
    auto bytes_read = ReadAll(input_fd, &input_files_str);
    if (bytes_read < 0) {
      LOG(FATAL) << "Failed to read input files. Error was \"" << input_fd->StrError() << "\"";
    }
  }
  std::vector<std::string> input_files = android::base::Split(input_files_str, "\n");
  bool found_config = false;
  for (const auto& file : input_files) {
    if (file.find("cuttlefish_config.json") != std::string::npos) {
      found_config = true;
      setenv(kCuttlefishConfigEnvVarName, file.c_str(), /* overwrite */ false);
    }
  }
  if (!found_config) {
    return RunnerExitCodes::kCuttlefishConfigurationInitError;
  }

  auto config = CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();

  auto log_path = instance.launcher_log_path();

  {
    std::ofstream launcher_log_ofstream(log_path.c_str());
    auto assembly_path = config->AssemblyPath("assemble_cvd.log");
    std::ifstream assembly_log_ifstream(assembly_path);
    if (assembly_log_ifstream) {
      auto assemble_log = ReadFile(assembly_path);
      launcher_log_ofstream << assemble_log;
    }
  }
  ::android::base::SetLogger(LogToStderrAndFiles({log_path}));

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

  auto used_tap_devices = TapInterfacesInUse();
  if (used_tap_devices.count(instance.wifi_tap_name())) {
    LOG(ERROR) << "Wifi TAP device already in use";
    return RunnerExitCodes::kTapDeviceInUse;
  } else if (used_tap_devices.count(instance.mobile_tap_name())) {
    LOG(ERROR) << "Mobile TAP device already in use";
    return RunnerExitCodes::kTapDeviceInUse;
  } else if (config->ethernet() &&
             used_tap_devices.count(instance.ethernet_tap_name())) {
    LOG(ERROR) << "Ethernet TAP device already in use";
  }

  auto vm_manager = GetVmManager(config->vm_manager(), config->target_arch());

#ifndef __ANDROID__
  // Check host configuration
  std::vector<std::string> config_commands;
  if (!ValidateHostConfiguration(&config_commands)) {
    LOG(ERROR) << "Validation of user configuration failed";
    std::cout << "Execute the following to correctly configure:" << std::endl;
    for (auto& command : config_commands) {
      std::cout << "  " << command << std::endl;
    }
    std::cout << "You may need to logout for the changes to take effect"
              << std::endl;
    return RunnerExitCodes::kInvalidHostConfiguration;
  }
#endif

  if (!WriteCuttlefishEnvironment(*config)) {
    LOG(ERROR) << "Unable to write cuttlefish environment file";
  }

  PrintStreamingInformation(*config);

  if (config->console()) {
    LOG(INFO) << kGreenColor << "To access the console run: screen "
              << instance.console_path() << kResetColor;
  } else {
    LOG(INFO) << kGreenColor
              << "Serial console is disabled; use -console=true to enable it"
              << kResetColor;
  }

  LOG(INFO) << kGreenColor
            << "The following files contain useful debugging information:"
            << kResetColor;
  LOG(INFO) << kGreenColor
            << "  Launcher log: " << instance.launcher_log_path()
            << kResetColor;
  LOG(INFO) << kGreenColor
            << "  Android's logcat output: " << instance.logcat_path()
            << kResetColor;
  LOG(INFO) << kGreenColor
            << "  Kernel log: " << instance.PerInstancePath("kernel.log")
            << kResetColor;
  LOG(INFO) << kGreenColor
            << "  Instance configuration: " << GetConfigFilePath(*config)
            << kResetColor;
  LOG(INFO) << kGreenColor
            << "  Instance environment: " << config->cuttlefish_env_path()
            << kResetColor;

  auto launcher_monitor_path = instance.launcher_monitor_socket_path();
  auto launcher_monitor_socket = SharedFD::SocketLocalServer(
      launcher_monitor_path.c_str(), false, SOCK_STREAM, 0666);
  if (!launcher_monitor_socket->IsOpen()) {
    LOG(ERROR) << "Error when opening launcher server: "
               << launcher_monitor_socket->StrError();
    return RunnerExitCodes::kMonitorCreationFailed;
  }
  SharedFD foreground_launcher_pipe;
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

  SharedFD reboot_notification;
  if (FLAGS_reboot_notification_fd >= 0) {
    reboot_notification = SharedFD::Dup(FLAGS_reboot_notification_fd);
    close(FLAGS_reboot_notification_fd);
  }

  // Monitor and restart host processes supporting the CVD
  ProcessMonitor process_monitor(config->restart_subprocesses());

  if (config->enable_metrics() == CuttlefishConfig::kYes) {
    process_monitor.AddCommands(LaunchMetrics());
  }
  process_monitor.AddCommands(LaunchModemSimulatorIfEnabled(*config));

  auto kernel_log_monitor = LaunchKernelLogMonitor(*config, 3);
  SharedFD boot_events_pipe = kernel_log_monitor.pipes[0];
  SharedFD adbd_events_pipe = kernel_log_monitor.pipes[1];
  SharedFD webrtc_events_pipe = kernel_log_monitor.pipes[2];
  kernel_log_monitor.pipes.clear();
  process_monitor.AddCommands(std::move(kernel_log_monitor.commands));

  CvdBootStateMachine boot_state_machine(foreground_launcher_pipe,
                                         reboot_notification, boot_events_pipe);

  process_monitor.AddCommands(LaunchRootCanal(*config));
  process_monitor.AddCommands(LaunchLogcatReceiver(*config));
  process_monitor.AddCommands(LaunchConfigServer(*config));
  process_monitor.AddCommands(LaunchTombstoneReceiver(*config));
  process_monitor.AddCommands(LaunchGnssGrpcProxyServerIfEnabled(*config));
  process_monitor.AddCommands(LaunchSecureEnvironment(*config));
  if (config->enable_host_bluetooth()) {
    process_monitor.AddCommands(LaunchBluetoothConnector(*config));
  }
  process_monitor.AddCommands(LaunchVehicleHalServerIfEnabled(*config));
  process_monitor.AddCommands(LaunchConsoleForwarderIfEnabled(*config));

  // The streamer needs to launch before the VMM because it serves on several
  // sockets (input devices, vsock frame server) when using crosvm.
  if (config->enable_vnc_server()) {
    process_monitor.AddCommands(LaunchVNCServer(*config));
  }
  if (config->enable_webrtc()) {
    process_monitor.AddCommands(LaunchWebRTC(*config, webrtc_events_pipe));
  }

  // Start the guest VM
  process_monitor.AddCommands(vm_manager->StartCommands(*config));

  // Start other host processes
  process_monitor.AddCommands(
      LaunchSocketVsockProxyIfEnabled(*config, adbd_events_pipe));
  process_monitor.AddCommands(LaunchAdbConnectorIfEnabled(*config));

  CHECK(process_monitor.StartAndMonitorProcesses())
      << "Could not start subprocesses";

  ServerLoop(launcher_monitor_socket, &process_monitor); // Should not return
  LOG(ERROR) << "The server loop returned, it should never happen!!";

  return RunnerExitCodes::kServerError;
}

} // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::RunCvdMain(argc, argv);
}
