//
// Copyright 2023 The Android Open Source Project
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
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

constexpr const size_t kBufferSize = 1024;

namespace cuttlefish {

Result<MonitorCommand> NfcConnector(
    const CuttlefishConfig::EnvironmentSpecific& environment,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<std::string> fifo_paths = {
      instance.PerInstanceInternalPath("nfc_fifo_vm.in"),
      instance.PerInstanceInternalPath("nfc_fifo_vm.out"),
  };
  std::vector<SharedFD> fifos;
  for (const auto& path : fifo_paths) {
    fifos.emplace_back(CF_EXPECT(SharedFD::Fifo(path, 0660)));
  }
  return Command(TcpConnectorBinary())
      .AddParameter("-fifo_out=", fifos[0])
      .AddParameter("-fifo_in=", fifos[1])
      .AddParameter("-data_path=", environment.casimir_nci_socket_path())
      .AddParameter("-buffer_size=", kBufferSize);
}

}  // namespace cuttlefish
