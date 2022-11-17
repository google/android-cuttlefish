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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/run_cvd/boot_state_machine.h"
#include "host/commands/run_cvd/launch/launch.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/commands/run_cvd/reporting.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/server_loop.h"
#include "host/commands/run_cvd/validate.h"
#include "host/libs/config/adb/adb.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/config_fragment.h"
#include "host/libs/config/custom_actions.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/inject.h"
#include "host/libs/metrics/metrics_receiver.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

namespace {

class CuttlefishEnvironment : public SetupFeature,
                              public DiagnosticInformation {
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

  // SetupFeature
  std::string Name() const override { return "CuttlefishEnvironment"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
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

class InstanceLifecycle : public LateInjected {
 public:
  INJECT(InstanceLifecycle(const CuttlefishConfig& config,
                           ServerLoop& server_loop))
      : config_(config), server_loop_(server_loop) {}

  Result<void> LateInject(fruit::Injector<>& injector) override {
    config_fragments_ = injector.getMultibindings<ConfigFragment>();
    setup_features_ = injector.getMultibindings<SetupFeature>();
    diagnostics_ = injector.getMultibindings<DiagnosticInformation>();
    return {};
  }

  Result<void> Run() {
    for (auto& fragment : config_fragments_) {
      CF_EXPECT(config_.LoadFragment(*fragment));
    }

    // One of the setup features can consume most output, so print this early.
    DiagnosticInformation::PrintAll(diagnostics_);

    CF_EXPECT(SetupFeature::RunSetup(setup_features_));

    CF_EXPECT(server_loop_.Run());

    return {};
  }

 private:
  const CuttlefishConfig& config_;
  ServerLoop& server_loop_;
  std::vector<ConfigFragment*> config_fragments_;
  std::vector<SetupFeature*> setup_features_;
  std::vector<DiagnosticInformation*> diagnostics_;
};

fruit::Component<> runCvdComponent(
    const CuttlefishConfig* config,
    const CuttlefishConfig::InstanceSpecific* instance) {
  return fruit::createComponent()
      .addMultibinding<DiagnosticInformation, CuttlefishEnvironment>()
      .addMultibinding<SetupFeature, CuttlefishEnvironment>()
      .addMultibinding<InstanceLifecycle, InstanceLifecycle>()
      .addMultibinding<LateInjected, InstanceLifecycle>()
      .bindInstance(*config)
      .bindInstance(*instance)
      .install(AdbConfigComponent)
      .install(AdbConfigFragmentComponent)
      .install(bootStateMachineComponent)
      .install(ConfigFlagPlaceholder)
      .install(CustomActionsComponent)
      .install(LaunchAdbComponent)
      .install(BluetoothConnectorComponent)
      .install(ConfigServerComponent)
      .install(ConsoleForwarderComponent)
      .install(GnssGrpcProxyServerComponent)
      .install(LogcatReceiverComponent)
      .install(KernelLogMonitorComponent)
      .install(MetricsServiceComponent)
      .install(OpenWrtComponent)
      .install(RootCanalComponent)
      .install(NetsimServerComponent)
      .install(SecureEnvComponent)
      .install(TombstoneReceiverComponent)
      .install(VehicleHalServerComponent)
      .install(WmediumdServerComponent)
      .install(launchModemComponent)
      .install(launchStreamerComponent)
      .install(serverLoopComponent)
      .install(validationComponent)
      .install(vm_manager::VmManagerComponent);
}

Result<void> StdinValid() {
  CF_EXPECT(!isatty(0),
            "stdin was a tty, expected to be passed the output of a"
            " previous stage. Did you mean to run launch_cvd?");
  CF_EXPECT(errno != EBADF,
            "stdin was not a valid file descriptor, expected to be passed the "
            "output of assemble_cvd. Did you mean to run launch_cvd?");
  return {};
}

Result<const CuttlefishConfig*> FindConfigFromStdin() {
  std::string input_files_str;
  {
    auto input_fd = SharedFD::Dup(0);
    auto bytes_read = ReadAll(input_fd, &input_files_str);
    CF_EXPECT(bytes_read >= 0, "Failed to read input files. Error was \""
                                   << input_fd->StrError() << "\"");
  }
  std::vector<std::string> input_files =
      android::base::Split(input_files_str, "\n");
  for (const auto& file : input_files) {
    if (file.find("cuttlefish_config.json") != std::string::npos) {
      setenv(kCuttlefishConfigEnvVarName, file.c_str(), /* overwrite */ false);
    }
  }
  return CF_EXPECT(CuttlefishConfig::Get());  // Null check
}

void ConfigureLogs(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance) {
  auto log_path = instance.launcher_log_path();

  if (!FileHasContent(log_path)) {
    std::ofstream launcher_log_ofstream(log_path.c_str());
    auto assembly_path = config.AssemblyPath("assemble_cvd.log");
    std::ifstream assembly_log_ifstream(assembly_path);
    if (assembly_log_ifstream) {
      auto assemble_log = ReadFile(assembly_path);
      launcher_log_ofstream << assemble_log;
    }
  }
  std::string prefix;
  if (config.Instances().size() > 1) {
    prefix = instance.instance_name() + ": ";
  }
  ::android::base::SetLogger(LogToStderrAndFiles({log_path}, prefix));
}

Result<void> ChdirIntoRuntimeDir(
    const CuttlefishConfig::InstanceSpecific& instance) {
  // Change working directory to the instance directory as early as possible to
  // ensure all host processes have the same working dir. This helps stop_cvd
  // find the running processes when it can't establish a communication with the
  // launcher.
  CF_EXPECT(chdir(instance.instance_dir().c_str()) == 0,
            "Unable to change dir into instance directory \""
                << instance.instance_dir() << "\": " << strerror(errno));
  return {};
}

}  // namespace

Result<void> RunCvdMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  CF_EXPECT(StdinValid(), "Invalid stdin");
  auto config = CF_EXPECT(FindConfigFromStdin());
  auto instance = config->ForDefaultInstance();

  ConfigureLogs(*config, instance);
  CF_EXPECT(ChdirIntoRuntimeDir(instance));

  fruit::Injector<> injector(runCvdComponent, config, &instance);

  for (auto& late_injected : injector.getMultibindings<LateInjected>()) {
    CF_EXPECT(late_injected->LateInject(injector));
  }

  MetricsReceiver::LogMetricsVMStart();

  auto instance_bindings = injector.getMultibindings<InstanceLifecycle>();
  CF_EXPECT(instance_bindings.size() == 1);
  CF_EXPECT(instance_bindings[0]->Run());  // Should not return

  return CF_ERR("The server loop returned, it should never happen!!");
}

} // namespace cuttlefish

int main(int argc, char** argv) {
  auto result = cuttlefish::RunCvdMain(argc, argv);
  if (result.ok()) {
    return 0;
  }
  LOG(ERROR) << result.error().Message();
  LOG(DEBUG) << result.error().Trace();
  abort();
}
