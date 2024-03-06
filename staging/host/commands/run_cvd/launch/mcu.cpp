//
// Copyright (C) 2023 The Android Open Source Project
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

#include <unordered_set>
#include <vector>

#include <android-base/logging.h>
#include <fruit/fruit.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/launch/launch.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"


// timeout for the MCU channels to be created after the start command is issued
#define MCU_START_TIMEOUT 30

namespace cuttlefish {
namespace {

class Mcu : public vm_manager::VmmDependencyCommand {
 public:
  INJECT(Mcu(const CuttlefishConfig::InstanceSpecific& instance,
             LogTeeCreator& log_tee))
      :instance_(instance), log_tee_(log_tee) {}

  Result<void> ResultSetup() override {
    if (!Enabled()) {
      return {};
    }

    mcu_dir_ = instance_.PerInstanceInternalPath("/mcu/");
    CF_EXPECT(EnsureDirectoryExists(mcu_dir_),
              "MCU directory cannot be created.");
    return {};
  }

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    if (!Enabled()) {
      return {};
    }

    auto start = instance_.mcu()["start-cmd"];
    CF_EXPECT(start.type() == Json::arrayValue,
              "mcu: config: start-cmd: array expected");
    CF_EXPECT(start.size() > 0, "mcu: config: empty start-cmd");
    Command command(android::base::StringReplace(start[0].asString(), "${bin}",
                                                 HostBinaryPath(""), true));

    for (unsigned int i = 1; i < start.size(); i++) {
      auto param = start[i].asString();
      param = android::base::StringReplace(param, "${wdir}", mcu_dir_, true);
      param = android::base::StringReplace(param, "${bin}", HostBinaryPath(""),
                                           true);
      command.AddParameter(param);
    }

    std::vector<MonitorCommand> commands;
    commands.emplace_back(CF_EXPECT(log_tee_.CreateLogTee(command, "mcu")));
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "MCU"; }
  bool Enabled() const override {
    return instance_.mcu().type() != Json::nullValue;
  }

  // StatusCheckCommandSource
  Result<void> WaitForAvailability() const {
    if (!Enabled()) {
      return {};
    }

    auto control = instance_.mcu()["control"]["path"];
    if (control.type() == Json::stringValue) {
      CF_EXPECT(WaitForFile(mcu_dir_ + "/" + control.asString(),
                            MCU_START_TIMEOUT));
    }
    auto uart0 = instance_.mcu()["uart0"]["path"];
    if (uart0.type() == Json::stringValue) {
      CF_EXPECT(WaitForFile(mcu_dir_ + "/" + uart0.asString(),
                            MCU_START_TIMEOUT));
    }
    return {};
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }

  std::string mcu_dir_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
};

}  // namespace

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
McuComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, Mcu>()
      .addMultibinding<CommandSource, Mcu>()
      .addMultibinding<SetupFeature, Mcu>();
}

}  // namespace cuttlefish
