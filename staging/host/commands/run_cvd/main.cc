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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/run_cvd/launch.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"
#include "host/libs/config/kernel_args.h"
#include "host/commands/kernel_log_monitor/kernel_log_server.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include <host/libs/vm_manager/crosvm_manager.h>
#include "host/libs/vm_manager/host_configuration.h"
#include "host/libs/vm_manager/vm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

using cuttlefish::ForCurrentInstance;
using cuttlefish::RunnerExitCodes;
using cuttlefish::vm_manager::GetVmManager;
using cuttlefish::vm_manager::ValidateHostConfiguration;

DEFINE_int32(powerwash_notification_fd, -1,
             "A file descriptor to notify when boot completes.");

namespace {

constexpr char kGreenColor[] = "\033[1;32m";
constexpr char kResetColor[] = "\033[0m";

cuttlefish::OnSocketReadyCb GetOnSubprocessExitCallback(
    const cuttlefish::CuttlefishConfig& config) {
  if (config.restart_subprocesses()) {
    return cuttlefish::ProcessMonitor::RestartOnExitCb;
  } else {
    return cuttlefish::ProcessMonitor::DoNotMonitorCb;
  }
}

// Maintains the state of the boot process, once a final state is reached
// (success or failure) it sends the appropriate exit code to the foreground
// launcher process
class CvdBootStateMachine {
 public:
  CvdBootStateMachine(cuttlefish::SharedFD fg_launcher_pipe,
                      cuttlefish::SharedFD powerwash_notification)
      : fg_launcher_pipe_(fg_launcher_pipe)
      , powerwash_notification_(powerwash_notification)
      , state_(kBootStarted) {}

  // Returns true if the machine is left in a final state
  bool OnBootEvtReceived(cuttlefish::SharedFD boot_events_pipe) {
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

  bool BootCompleted() const {
    return state_ & kGuestBootCompleted;
  }

  bool BootFailed() const {
    return state_ & kGuestBootFailed;
  }

 private:
  void SendExitCode(cuttlefish::RunnerExitCodes exit_code,
                    cuttlefish::SharedFD fd) {
    fd->Write(&exit_code, sizeof(exit_code));
    // The foreground process will exit after receiving the exit code, if we try
    // to write again we'll get a SIGPIPE
    fd->Close();
  }
  bool MaybeWriteNotification() {
    std::vector<cuttlefish::SharedFD> fds =
        {powerwash_notification_, fg_launcher_pipe_};
    for (auto& fd : fds) {
      if (fd->IsOpen()) {
        if (BootCompleted()) {
          SendExitCode(cuttlefish::RunnerExitCodes::kSuccess, fd);
        } else if (state_ & kGuestBootFailed) {
          SendExitCode(
              cuttlefish::RunnerExitCodes::kVirtualDeviceBootFailed, fd);
        }
      }
    }
    // Either we sent the code before or just sent it, in any case the state is
    // final
    return BootCompleted() || (state_ & kGuestBootFailed);
  }

  cuttlefish::SharedFD fg_launcher_pipe_;
  cuttlefish::SharedFD powerwash_notification_;
  int state_;
  static const int kBootStarted = 0;
  static const int kGuestBootCompleted = 1 << 0;
  static const int kGuestBootFailed = 1 << 1;
};

// Abuse the process monitor to make it call us back when boot events are ready
void SetUpHandlingOfBootEvents(
    cuttlefish::ProcessMonitor* process_monitor, cuttlefish::SharedFD boot_events_pipe,
    std::shared_ptr<CvdBootStateMachine> state_machine) {
  process_monitor->MonitorExistingSubprocess(
      // An unused command, so logs are desciptive
      cuttlefish::Command("boot_events_listener"),
      // An unused subprocess, with the boot events pipe as control socket
      cuttlefish::Subprocess(-1, boot_events_pipe),
      [boot_events_pipe, state_machine](cuttlefish::MonitorEntry*) {
        auto sent_code = state_machine->OnBootEvtReceived(boot_events_pipe);
        return !sent_code;
      });
}

bool WriteCuttlefishEnvironment(const cuttlefish::CuttlefishConfig& config) {
  auto env = cuttlefish::SharedFD::Open(config.cuttlefish_env_path().c_str(),
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
cuttlefish::SharedFD DaemonizeLauncher(const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  cuttlefish::SharedFD read_end, write_end;
  if (!cuttlefish::SharedFD::Pipe(&read_end, &write_end)) {
    LOG(ERROR) << "Unable to create pipe";
    return cuttlefish::SharedFD(); // a closed FD
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
      LOG(INFO) << cuttlefish::kBootCompletedMessage;
    } else {
      LOG(INFO) << cuttlefish::kBootFailedMessage;
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
        cuttlefish::SharedFD::Open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (!log->IsOpen()) {
      LOG(ERROR) << "Failed to create launcher log file: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    ::android::base::SetLogger(cuttlefish::TeeLogger({
      {cuttlefish::LogFileSeverity(), log},
    }));
    auto dev_null = cuttlefish::SharedFD::Open("/dev/null", O_RDONLY);
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

bool CreateQcowOverlay(const std::string& crosvm_path,
                       const std::string& backing_file,
                       const std::string& output_overlay_path) {
  cuttlefish::Command crosvm_qcow2_cmd(crosvm_path);
  crosvm_qcow2_cmd.AddParameter("create_qcow2");
  crosvm_qcow2_cmd.AddParameter("--backing_file=", backing_file);
  crosvm_qcow2_cmd.AddParameter(output_overlay_path);
  int success = crosvm_qcow2_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run crosvm create_qcow2. Exited with status " << success;
    return false;
  }
  return true;
}

bool PowerwashFiles() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Could not load the config.";
    return false;
  }
  using cuttlefish::CreateBlankImage;
  auto instance = config->ForDefaultInstance();

  // TODO(schuffelen): Create these FIFOs in assemble_cvd instead of run_cvd.
  std::vector<std::string> pipes = {
    instance.kernel_log_pipe_name(),
    instance.console_in_pipe_name(),
    instance.console_out_pipe_name(),
    instance.logcat_pipe_name(),
    instance.PerInstanceInternalPath("keymaster_fifo_vm.in"),
    instance.PerInstanceInternalPath("keymaster_fifo_vm.out"),
    instance.PerInstanceInternalPath("gatekeeper_fifo_vm.in"),
    instance.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
  };
  for (const auto& pipe : pipes) {
    unlink(pipe.c_str());
  }

// TODO(schuffelen): Clean up duplication with assemble_cvd
  auto kregistry_path = instance.access_kregistry_path();
  unlink(kregistry_path.c_str());
  CreateBlankImage(kregistry_path, 2 /* mb */, "none");

  auto pstore_path = instance.pstore_path();
  unlink(pstore_path.c_str());
  CreateBlankImage(pstore_path, 2 /* mb */, "none");

  auto sdcard_path = instance.sdcard_path();
  auto sdcard_size = cuttlefish::FileSize(sdcard_path);
  unlink(sdcard_path.c_str());
  // round up
  auto sdcard_mb_size = (sdcard_size + (1 << 20) - 1) / (1 << 20);
  LOG(DEBUG) << "Size in mb is " << sdcard_mb_size;
  CreateBlankImage(sdcard_path, sdcard_mb_size, "sdcard");

  auto overlay_path = instance.PerInstancePath("overlay.img");
  unlink(overlay_path.c_str());
  if (!CreateQcowOverlay(
      config->crosvm_binary(), instance.composite_disk_path(), overlay_path)) {
    LOG(ERROR) << "CreateQcowOverlay failed";
    return false;
  }
  return true;
}

void ServerLoop(cuttlefish::SharedFD server,
                cuttlefish::ProcessMonitor* process_monitor) {
  while (true) {
    // TODO: use select to handle simultaneous connections.
    auto client = cuttlefish::SharedFD::Accept(*server);
    cuttlefish::LauncherAction action;
    while (client->IsOpen() && client->Read(&action, sizeof(action)) > 0) {
      switch (action) {
        case cuttlefish::LauncherAction::kStop:
          if (process_monitor->StopMonitoredProcesses()) {
            auto response = cuttlefish::LauncherResponse::kSuccess;
            client->Write(&response, sizeof(response));
            std::exit(0);
          } else {
            auto response = cuttlefish::LauncherResponse::kError;
            client->Write(&response, sizeof(response));
          }
          break;
        case cuttlefish::LauncherAction::kStatus: {
          // TODO(schuffelen): Return more information on a side channel
          auto response = cuttlefish::LauncherResponse::kSuccess;
          client->Write(&response, sizeof(response));
          break;
        }
        case cuttlefish::LauncherAction::kPowerwash: {
          LOG(INFO) << "Received a Powerwash request from the monitor socket";
          if (!process_monitor->StopMonitoredProcesses()) {
            LOG(ERROR) << "Stopping processes failed.";
            auto response = cuttlefish::LauncherResponse::kError;
            client->Write(&response, sizeof(response));
            break;
          }
          if (!PowerwashFiles()) {
            LOG(ERROR) << "Powerwashing files failed.";
            auto response = cuttlefish::LauncherResponse::kError;
            client->Write(&response, sizeof(response));
            break;
          }
          auto response = cuttlefish::LauncherResponse::kSuccess;
          client->Write(&response, sizeof(response));

          auto config = cuttlefish::CuttlefishConfig::Get();
          auto config_path = config->AssemblyPath("cuttlefish_config.json");
          auto followup_stdin =
              cuttlefish::SharedFD::MemfdCreate("pseudo_stdin");
          cuttlefish::WriteAll(followup_stdin, config_path + "\n");
          followup_stdin->LSeek(0, SEEK_SET);
          followup_stdin->UNMANAGED_Dup2(0);

          auto argv_vec = gflags::GetArgvs();
          char** argv = new char*[argv_vec.size() + 2];
          for (size_t i = 0; i < argv_vec.size(); i++) {
            argv[i] = argv_vec[i].data();
          }
          int notification_fd = client->UNMANAGED_Dup();
          // Will take precedence over any earlier arguments.
          std::string powerwash_notification =
              "-powerwash_notification_fd=" + std::to_string(notification_fd);
          argv[argv_vec.size()] = powerwash_notification.data();
          argv[argv_vec.size() + 1] = nullptr;

          execv("/proc/self/exe", argv);
          // execve should not return, so something went wrong.
          PLOG(ERROR) << "execv returned: ";
          response = cuttlefish::LauncherResponse::kError;
          client->Write(&response, sizeof(response));
          break;
        }
        default:
          LOG(ERROR) << "Unrecognized launcher action: "
                     << static_cast<char>(action);
          auto response = cuttlefish::LauncherResponse::kError;
          client->Write(&response, sizeof(response));
      }
    }
  }
}

std::string GetConfigFilePath(const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.PerInstancePath("cuttlefish_config.json");
}

void PrintStreamingInformation(const cuttlefish::CuttlefishConfig& config) {
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

int main(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  if (isatty(0)) {
    LOG(FATAL) << "stdin was a tty, expected to be passed the output of a previous stage. "
               << "Did you mean to run launch_cvd?";
    return cuttlefish::RunnerExitCodes::kInvalidHostConfiguration;
  } else {
    int error_num = errno;
    if (error_num == EBADF) {
      LOG(FATAL) << "stdin was not a valid file descriptor, expected to be passed the output "
                 << "of assemble_cvd. Did you mean to run launch_cvd?";
      return cuttlefish::RunnerExitCodes::kInvalidHostConfiguration;
    }
  }

  std::string input_files_str;
  {
    auto input_fd = cuttlefish::SharedFD::Dup(0);
    auto bytes_read = cuttlefish::ReadAll(input_fd, &input_files_str);
    if (bytes_read < 0) {
      LOG(FATAL) << "Failed to read input files. Error was \"" << input_fd->StrError() << "\"";
    }
  }
  std::vector<std::string> input_files = android::base::Split(input_files_str, "\n");
  bool found_config = false;
  for (const auto& file : input_files) {
    if (file.find("cuttlefish_config.json") != std::string::npos) {
      found_config = true;
      setenv(cuttlefish::kCuttlefishConfigEnvVarName, file.c_str(), /* overwrite */ false);
    }
  }
  if (!found_config) {
    return RunnerExitCodes::kCuttlefishConfigurationInitError;
  }

  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();

  auto log_path = instance.launcher_log_path();

  {
    std::ofstream launcher_log_ofstream(log_path.c_str());
    auto assembly_path = config->AssemblyPath("assemble_cvd.log");
    std::ifstream assembly_log_ifstream(assembly_path);
    if (assembly_log_ifstream) {
      auto assemble_log = cuttlefish::ReadFile(assembly_path);
      launcher_log_ofstream << assemble_log;
    }
  }
  ::android::base::SetLogger(cuttlefish::LogToStderrAndFiles({log_path}));

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

  auto used_tap_devices = cuttlefish::TapInterfacesInUse();
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

  auto vm_manager = GetVmManager(config->vm_manager());

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
  auto launcher_monitor_socket = cuttlefish::SharedFD::SocketLocalServer(
      launcher_monitor_path.c_str(), false, SOCK_STREAM, 0666);
  if (!launcher_monitor_socket->IsOpen()) {
    LOG(ERROR) << "Error when opening launcher server: "
               << launcher_monitor_socket->StrError();
    return cuttlefish::RunnerExitCodes::kMonitorCreationFailed;
  }
  cuttlefish::SharedFD foreground_launcher_pipe;
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

  cuttlefish::SharedFD powerwash_notification;
  if (FLAGS_powerwash_notification_fd >= 0) {
    powerwash_notification =
        cuttlefish::SharedFD::Dup(FLAGS_powerwash_notification_fd);
    close(FLAGS_powerwash_notification_fd);
  }

  auto boot_state_machine =
      std::make_shared<CvdBootStateMachine>(
          foreground_launcher_pipe, powerwash_notification);

  // Monitor and restart host processes supporting the CVD
  cuttlefish::ProcessMonitor process_monitor;

  if (config->enable_metrics() == cuttlefish::CuttlefishConfig::kYes) {
    LaunchMetrics(&process_monitor, *config);
  }
  LaunchModemSimulatorIfEnabled(*config, &process_monitor);

  auto event_pipes =
      LaunchKernelLogMonitor(*config, &process_monitor, 3);
  cuttlefish::SharedFD boot_events_pipe = event_pipes[0];
  cuttlefish::SharedFD adbd_events_pipe = event_pipes[1];
  cuttlefish::SharedFD webrtc_events_pipe = event_pipes[2];
  event_pipes.clear();

  SetUpHandlingOfBootEvents(&process_monitor, boot_events_pipe,
                            boot_state_machine);

  LaunchLogcatReceiver(*config, &process_monitor);
  LaunchConfigServer(*config, &process_monitor);
  LaunchTombstoneReceiver(*config, &process_monitor);
  LaunchGnssGrpcProxyServerIfEnabled(*config, &process_monitor);
  LaunchSecureEnvironment(&process_monitor, *config);
  LaunchVerhicleHalServerIfEnabled(*config, &process_monitor);
  LaunchConsoleForwarderIfEnabled(*config, &process_monitor);

  // The streamer needs to launch before the VMM because it serves on several
  // sockets (input devices, vsock frame server) when using crosvm.
  if (config->enable_vnc_server()) {
    LaunchVNCServer(
        *config, &process_monitor, GetOnSubprocessExitCallback(*config));
  }
  if (config->enable_webrtc()) {
    LaunchWebRTC(&process_monitor, *config, webrtc_events_pipe);
  }

  auto kernel_args =
      cuttlefish::KernelCommandLineFromConfig(
          *config, config->ForDefaultInstance());

  // Start the guest VM
  auto vmm_commands = vm_manager->StartCommands(
      *config, android::base::Join(kernel_args, " "));
  for (auto& vmm_cmd: vmm_commands) {
      process_monitor.StartSubprocess(std::move(vmm_cmd),
                                      GetOnSubprocessExitCallback(*config));
  }

  // Start other host processes
  LaunchSocketVsockProxyIfEnabled(&process_monitor, *config, adbd_events_pipe);
  LaunchAdbConnectorIfEnabled(&process_monitor, *config);

  ServerLoop(launcher_monitor_socket, &process_monitor); // Should not return
  LOG(ERROR) << "The server loop returned, it should never happen!!";
  return cuttlefish::RunnerExitCodes::kServerError;
}
