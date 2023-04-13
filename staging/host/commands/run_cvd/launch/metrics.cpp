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

class MetricsService : public CommandSource {
 public:
  INJECT(MetricsService(const CuttlefishConfig& config)) : config_(config) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(MetricsBinary());
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "MetricsService"; }
  bool Enabled() const override {
    return config_.enable_metrics() == CuttlefishConfig::kYes;
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

 private:
  const CuttlefishConfig& config_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig>>
MetricsServiceComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, MetricsService>()
      .addMultibinding<SetupFeature, MetricsService>();
}

}  // namespace cuttlefish
