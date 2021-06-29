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
#include "host/commands/run_cvd/reporting.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/server_loop.h"
#include "host/commands/run_cvd/validate.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

using vm_manager::GetVmManager;

namespace {

class CuttlefishEnvironment : public Feature, public DiagnosticInformation {
 public:
  INJECT(
      CuttlefishEnvironment(const CuttlefishConfig& config,
                            const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    auto config_path = instance_.PerInstancePath("cuttlefish_config.json");
    return {
        "Launcher log: " + instance_.launcher_log_path(),
        "Instance configuration: " + config_path,
        "Instance environment: " + config_.cuttlefish_env_path(),
    };
  }

  // Feature
  bool Enabled() const override { return true; }
  std::string Name() const override { return "CuttlefishEnvironment"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    auto env =
        SharedFD::Open(config_.cuttlefish_env_path(), O_CREAT | O_RDWR, 0755);
    if (!env->IsOpen()) {
      LOG(ERROR) << "Unable to create cuttlefish.env file";
      return false;
    }
    std::string config_env = "export CUTTLEFISH_PER_INSTANCE_PATH=\"" +
                             instance_.PerInstancePath(".") + "\"\n";
    config_env += "export ANDROID_SERIAL=" + instance_.adb_ip_and_port() + "\n";
    auto written = WriteAll(env, config_env);
    if (written != config_env.size()) {
      LOG(ERROR) << "Failed to write all of \"" << config_env << "\", "
                 << "only wrote " << written << " bytes. Error was "
                 << env->StrError();
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

fruit::Component<ServerLoop> runCvdComponent(
    const CuttlefishConfig* config,
    const CuttlefishConfig::InstanceSpecific* instance) {
  return fruit::createComponent()
      .addMultibinding<DiagnosticInformation, CuttlefishEnvironment>()
      .addMultibinding<Feature, CuttlefishEnvironment>()
      .bindInstance(*config)
      .bindInstance(*instance)
      .install(bootStateMachineComponent)
      .install(launchAdbComponent)
      .install(launchComponent)
      .install(launchModemComponent)
      .install(launchStreamerComponent)
      .install(serverLoopComponent)
      .install(validationComponent);
}

bool IsStdinValid() {
  if (isatty(0)) {
    LOG(ERROR) << "stdin was a tty, expected to be passed the output of a "
                  "previous stage. "
               << "Did you mean to run launch_cvd?";
    return false;
  } else {
    int error_num = errno;
    if (error_num == EBADF) {
      LOG(ERROR) << "stdin was not a valid file descriptor, expected to be "
                    "passed the output "
                 << "of assemble_cvd. Did you mean to run launch_cvd?";
      return false;
    }
  }
  return true;
}

const CuttlefishConfig* FindConfigFromStdin() {
  std::string input_files_str;
  {
    auto input_fd = SharedFD::Dup(0);
    auto bytes_read = ReadAll(input_fd, &input_files_str);
    if (bytes_read < 0) {
      LOG(ERROR) << "Failed to read input files. Error was \""
                 << input_fd->StrError() << "\"";
      return nullptr;
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
  return CuttlefishConfig::Get();
}

void ConfigureLogs(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance) {
  auto log_path = instance.launcher_log_path();

  std::ofstream launcher_log_ofstream(log_path.c_str());
  auto assembly_path = config.AssemblyPath("assemble_cvd.log");
  std::ifstream assembly_log_ifstream(assembly_path);
  if (assembly_log_ifstream) {
    auto assemble_log = ReadFile(assembly_path);
    launcher_log_ofstream << assemble_log;
  }
  ::android::base::SetLogger(LogToStderrAndFiles({log_path}));
}

bool ChdirIntoRuntimeDir(const CuttlefishConfig::InstanceSpecific& instance) {
  // Change working directory to the instance directory as early as possible to
  // ensure all host processes have the same working dir. This helps stop_cvd
  // find the running processes when it can't establish a communication with the
  // launcher.
  auto chdir_ret = chdir(instance.instance_dir().c_str());
  if (chdir_ret != 0) {
    PLOG(ERROR) << "Unable to change dir into instance directory ("
                << instance.instance_dir() << "): ";
    return false;
  }
  return true;
}

}  // namespace

int RunCvdMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  CHECK(IsStdinValid()) << "Invalid stdin";
  auto config = FindConfigFromStdin();
  CHECK(config) << "Could not find config";
  auto instance = config->ForDefaultInstance();

  ConfigureLogs(*config, instance);
  CHECK(ChdirIntoRuntimeDir(instance)) << "Could not enter runtime dir";

  fruit::Injector<ServerLoop> injector(runCvdComponent, config, &instance);

  // One of the setup features can consume most output, so print this early.
  DiagnosticInformation::PrintAll(
      injector.getMultibindings<DiagnosticInformation>());

  const auto& features = injector.getMultibindings<Feature>();
  CHECK(Feature::RunSetup(features)) << "Failed to run feature setup.";

  // Monitor and restart host processes supporting the CVD
  ProcessMonitor process_monitor(config->restart_subprocesses());

  for (auto& command_source : injector.getMultibindings<CommandSource>()) {
    if (command_source->Enabled()) {
      process_monitor.AddCommands(command_source->Commands());
    }
  }

  // The streamer needs to launch before the VMM because it serves on several
  // sockets (input devices, vsock frame server) when using crosvm.

  // Start the guest VM
  auto vm_manager = GetVmManager(config->vm_manager(), config->target_arch());
  process_monitor.AddCommands(vm_manager->StartCommands(*config));

  CHECK(process_monitor.StartAndMonitorProcesses())
      << "Could not start subprocesses";

  injector.get<ServerLoop&>().Run(process_monitor);  // Should not return
  LOG(ERROR) << "The server loop returned, it should never happen!!";

  return RunnerExitCodes::kServerError;
}

} // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::RunCvdMain(argc, argv);
}
