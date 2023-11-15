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

Result<std::vector<MonitorCommand>> Pica(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance,
    LogTeeCreator& log_tee) {
  if (!instance.start_pica()) {
    return {};
  }
  auto pcap_dir = instance.PerInstanceLogPath("/pica/");
  CF_EXPECT(EnsureDirectoryExists(pcap_dir),
            "Pica pcap directory cannot be created.");

  auto pica = Command(PicaBinary())
                  .AddParameter("--uci-port=", config.pica_uci_port())
                  .AddParameter("--pcapng-dir=", pcap_dir);

  std::vector<MonitorCommand> commands;
  commands.emplace_back(CF_EXPECT(log_tee.CreateLogTee(pica, "pica")));
  commands.emplace_back(std::move(pica));
  return commands;
}

}  // namespace cuttlefish
