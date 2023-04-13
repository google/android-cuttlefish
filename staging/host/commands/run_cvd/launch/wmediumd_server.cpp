//
// Copyright (C) 2019 The Android Open Source Project
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

#include "host/commands/run_cvd/launch/launch.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class WmediumdServer : public CommandSource {
 public:
  INJECT(WmediumdServer(const CuttlefishConfig& config,
                        const CuttlefishConfig::InstanceSpecific& instance,
                        LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command cmd(WmediumdBinary());
    cmd.AddParameter("-u", config_.vhost_user_mac80211_hwsim());
    cmd.AddParameter("-a", config_.wmediumd_api_server_socket());
    cmd.AddParameter("-c", config_path_);

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(log_tee_.CreateLogTee(cmd, "wmediumd")));
    commands.emplace_back(std::move(cmd));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "WmediumdServer"; }
  bool Enabled() const override {
    return instance_.start_wmediumd();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    // If wmediumd configuration is given, use it
    if (!config_.wmediumd_config().empty()) {
      config_path_ = config_.wmediumd_config();
      return {};
    }
    // Otherwise, generate wmediumd configuration using the current wifi mac
    // prefix before start
    config_path_ = instance_.PerInstanceInternalPath("wmediumd.cfg");
    Command gen_config_cmd(WmediumdGenConfigBinary());
    gen_config_cmd.AddParameter("-o", config_path_);
    gen_config_cmd.AddParameter("-p", instance_.wifi_mac_prefix());

    int success = gen_config_cmd.Start().Wait();
    CF_EXPECT(success == 0, "Unable to run " << gen_config_cmd.Executable()
                                             << ". Exited with status "
                                             << success);
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
  std::string config_path_;
};

}  // namespace

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
WmediumdServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, WmediumdServer>()
      .addMultibinding<SetupFeature, WmediumdServer>();
}

}  // namespace cuttlefish
