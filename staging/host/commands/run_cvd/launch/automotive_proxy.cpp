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

#include "host/commands/run_cvd/launch/launch.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/fruit.h>

#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class AutomotiveProxyService : public CommandSource {
 public:
  INJECT(AutomotiveProxyService(const CuttlefishConfig& config))
      : config_(config) {}

  // Command Source
  Result<std::vector<MonitorCommand>> Commands() override {
    // Create the Automotive Proxy command
    Command automotiveProxy(AutomotiveProxyBinary());
    automotiveProxy.AddParameter(
        DefaultHostArtifactsPath("etc/automotive/proxy_config.json"));

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(automotiveProxy));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "automotive_vsock_proxy"; }
  bool Enabled() const override { return config_.enable_automotive_proxy(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override { return {}; }

  const CuttlefishConfig& config_;
};
}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig>>
AutomotiveProxyComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, AutomotiveProxyService>()
      .addMultibinding<SetupFeature, AutomotiveProxyService>();
}

}  // namespace cuttlefish
