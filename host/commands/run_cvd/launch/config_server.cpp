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
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class ConfigServer : public CommandSource {
 public:
  INJECT(ConfigServer(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(ConfigServerBinary());
    command.AddParameter("-server_fd=", socket_);
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "ConfigServer"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    auto port = instance_.config_server_port();
    socket_ = SharedFD::VsockServer(port, SOCK_STREAM);
    CF_EXPECT(socket_->IsOpen(),
              "Unable to create configuration server socket: "
                  << socket_->StrError());
    return {};
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD socket_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
ConfigServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, ConfigServer>()
      .addMultibinding<SetupFeature, ConfigServer>();
}

}  // namespace cuttlefish
