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

#include <unordered_set>
#include <vector>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class Pica : public CommandSource {
 public:
  INJECT(Pica(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance,
                   LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  Result<void> ResultSetup() override {
    if (!Enabled()) {
      return {};
    }

    pcap_dir_ = instance_.PerInstanceLogPath("/pica/");
    CF_EXPECT(EnsureDirectoryExists(pcap_dir_),
              "Pica pcap directory cannot be created.");
    return {};
  }

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    if (!Enabled()) {
      return {};
    }
    Command command(PicaBinary());

    // UCI server port
    command.AddParameter("--uci-port=", config_.pica_uci_port());

    // pcap dir
    command.AddParameter("--pcapng-dir=", std::move(pcap_dir_));

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(log_tee_.CreateLogTee(command, "pica")));
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "Pica"; }
  bool Enabled() const override {
    return config_.enable_host_uwb_connector() && instance_.start_pica();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  std::string pcap_dir_;
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
};

}  // namespace

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
PicaComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, Pica>()
      .addMultibinding<SetupFeature, Pica>();
}

}  // namespace cuttlefish
