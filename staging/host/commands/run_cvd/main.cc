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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/run_cvd/boot_state_machine.h"
#include "host/commands/run_cvd/launch.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/server_loop.h"
#include "host/commands/run_cvd/validate.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

using vm_manager::GetVmManager;

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

fruit::Component<const CuttlefishConfig,
                 const CuttlefishConfig::InstanceSpecific>
configComponent() {
  static auto config = CuttlefishConfig::Get();
  CHECK(config) << "Could not load config.";
  static auto instance = config->ForDefaultInstance();
  return fruit::createComponent().bindInstance(*config).bindInstance(instance);
}

fruit::Component<> runCvdComponent() {
  return fruit::createComponent()
      .install(bootStateMachineComponent)
      .install(configComponent)
      .install(launchAdbComponent)
      .install(launchComponent)
      .install(launchModemComponent)
      .install(launchStreamerComponent)
      .install(validationComponent);
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

  auto vm_manager = GetVmManager(config->vm_manager(), config->target_arch());

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

  // Monitor and restart host processes supporting the CVD
  ProcessMonitor process_monitor(config->restart_subprocesses());

  fruit::Injector<> injector(runCvdComponent);
  const auto& features = injector.getMultibindings<Feature>();
  CHECK(Feature::RunSetup(features)) << "Failed to run feature setup.";

  for (auto& command_source : injector.getMultibindings<CommandSource>()) {
    if (command_source->Enabled()) {
      process_monitor.AddCommands(command_source->Commands());
    }
  }

  // The streamer needs to launch before the VMM because it serves on several
  // sockets (input devices, vsock frame server) when using crosvm.

  // Start the guest VM
  process_monitor.AddCommands(vm_manager->StartCommands(*config));

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
