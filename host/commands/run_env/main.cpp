/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/run_env/services/services.h"
#include "host/commands/run_env/services/wmediumd_server.h"
#include "host/libs/config/inject.h"
#include "host/libs/process_monitor/process_monitor.h"

DEFINE_string(env_name, CF_DEFAULTS_ENV_NAME, "environment name to create");

namespace cuttlefish {

class EnvironmentLauncher : public LateInjected {
 public:
  INJECT(EnvironmentLauncher(
      const CuttlefishConfig::EnvironmentSpecific& environment))
      : environment_(environment){};

  Result<void> LateInject(fruit::Injector<>& injector) override {
    setup_features_ = injector.getMultibindings<SetupFeature>();
    status_check_command_sources_ =
        injector.getMultibindings<StatusCheckCommandSource>();
    return {};
  }

  Result<void> Run() {
    CF_EXPECT(SetupFeature::RunSetup(setup_features_));

    ProcessMonitor::Properties process_monitor_properties;

    for (auto& command_source : status_check_command_sources_) {
      if (command_source->Enabled()) {
        auto commands = CF_EXPECT(command_source->Commands());
        process_monitor_properties.AddCommands(std::move(commands));
      }
    }

    ProcessMonitor process_monitor(std::move(process_monitor_properties));

    CF_EXPECT(process_monitor.StartAndMonitorProcesses());

    for (auto& command_source : status_check_command_sources_) {
      if (command_source->Enabled()) {
        CF_EXPECT(command_source->WaitForAvailability());
      }
    }

    CF_EXPECT(RunServerLoop());

    return {};
  }

 private:
  Result<void> RunServerLoop() {
    auto server_socket_path = environment_.control_socket_path();

    server_ = SharedFD::SocketLocalServer(server_socket_path.c_str(), false,
                                          SOCK_STREAM, 0666);

    CF_EXPECTF(server_->IsOpen(), "Error while opening server socket: {}",
               server_->StrError());

    while (true) {
      auto client = SharedFD::Accept(*server_);

      while (client->IsOpen()) {
        uint32_t command = 0;

        if (ReadExactBinary(client, &command) != sizeof(command)) {
          break;
        }
      }
    }
  }

  SharedFD server_;
  std::vector<SetupFeature*> setup_features_;
  std::vector<StatusCheckCommandSource*> status_check_command_sources_;
  const CuttlefishConfig::EnvironmentSpecific& environment_;
};

fruit::Component<> runEnvComponent(
    const CuttlefishConfig* config,
    const CuttlefishConfig::EnvironmentSpecific* environment) {
  return fruit::createComponent()
      .addMultibinding<EnvironmentLauncher, EnvironmentLauncher>()
      .addMultibinding<LateInjected, EnvironmentLauncher>()
      .bindInstance(*config)
      .bindInstance(*environment)
#ifdef __linux__
      .install(WmediumdServerComponent);
#endif /* __linux__ */
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

void ConfigureLogs(const CuttlefishConfig::EnvironmentSpecific& environment) {
  auto log_path = environment.launcher_log_path();

  std::string prefix = environment.environment_name() + ": ";

  ::android::base::SetLogger(LogToStderrAndFiles({log_path}, prefix));
}

Result<void> RunEnvMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  CF_EXPECT(StdinValid(), "Invalid stdin");

  auto config = CF_EXPECT(FindConfigFromStdin());
  auto environment = config->ForEnvironment(FLAGS_env_name);

  ConfigureLogs(environment);

  fruit::Injector<> env_injector(runEnvComponent, config, &environment);

  for (auto& late_injected : env_injector.getMultibindings<LateInjected>()) {
    CF_EXPECT(late_injected->LateInject(env_injector));
  }

  auto env_launcher = env_injector.getMultibindings<EnvironmentLauncher>();

  CF_EXPECT(env_launcher.size() == 1);
  CF_EXPECT(env_launcher[0]->Run());

  return CF_ERR("The env loop returned, it should never happen!!");
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto result = cuttlefish::RunEnvMain(argc, argv);
  if (result.ok()) {
    return 0;
  }
  LOG(ERROR) << result.error().Message();
  LOG(DEBUG) << result.error().Trace();
  abort();
}
