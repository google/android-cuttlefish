//
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/run_cvd/launch/ti50_emulator.h"

#include <signal.h>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "fruit/component.h"
#include "fruit/fruit_forward_decls.h"
#include "fruit/macro.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/process/subprocess.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class Ti50Emulator : public vm_manager::VmmDependencyCommand {
 public:
  INJECT(Ti50Emulator(const CuttlefishConfig::InstanceSpecific& instance,
                      LogTeeCreator& log_tee))
      : instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    if (!Enabled()) {
      LOG(ERROR) << "ti50 emulator is not enabled";
      return {};
    }

#ifndef __linux__
    return CF_ERR("Cannot start the ti50 emulator outside of Linux.");
#endif

    Command command(instance_.ti50_emulator());
    command.AddParameter("-s");
    command.AddParameter("--autonomous_ap_reset");
    command.AddParameter("-p=", instance_.instance_dir());
    command.SetStopper(
        KillSubprocessFallback(SubprocessStopper([](Subprocess* proc) {
          return proc->SendSignal(SIGTERM).has_value()
                     ? StopperResult::kSuccess
                     : StopperResult::kFailure;
        })));

    std::vector<MonitorCommand> commands;
    commands.emplace_back(
        CF_EXPECT(log_tee_.CreateFullLogTee(command, "ti50")));
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "Ti50Emulator"; }
  bool Enabled() const override { return !instance_.ti50_emulator().empty(); }

  // StatusCheckCommandSource
  Result<void> WaitForAvailability() override {
    if (!Enabled()) {
      return {};
    }

    Command status_cmd(instance_.ti50_emulator());
    status_cmd.AddParameter("-p=", instance_.instance_dir());
    status_cmd.AddParameter("status");
    status_cmd.AddParameter("--timeout=5s");

    std::string stdout_str;
    std::string stderr_str;
    const int ret = RunWithManagedStdio(std::move(status_cmd), nullptr,
                                        &stdout_str, &stderr_str);
    CF_EXPECTF(ret == 0, "ti50 emulator status command failed (exit {}): {}",
               ret, stderr_str);
    const Result<Json::Value> status_json = CF_EXPECT(ParseJson(stdout_str));
    CF_EXPECT(status_json->isObject() && (*status_json)["status"] == "ok",
              "ti50 emulator status not ok: " << stdout_str);
    LOG(INFO) << "ti50 emulator is ready: " << stdout_str;
    return {};
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override { return {}; }

  const CuttlefishConfig::InstanceSpecific instance_;
  LogTeeCreator& log_tee_;
};
}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig, LogTeeCreator,
                                 const CuttlefishConfig::InstanceSpecific>>
Ti50EmulatorComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, Ti50Emulator>()
      .addMultibinding<CommandSource, Ti50Emulator>()
      .addMultibinding<SetupFeature, Ti50Emulator>();
}

}  // namespace cuttlefish
