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

#define UCI_HEADER_SIZE 4
#define UCI_MAX_PAYLOAD_SIZE 255
#define UCI_MAX_PACKET_SIZE (UCI_HEADER_SIZE + UCI_MAX_PAYLOAD_SIZE)

constexpr const size_t kBufferSize = UCI_MAX_PACKET_SIZE * 2;

namespace cuttlefish {

Result<MonitorCommand> UwbConnector(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<std::string> fifo_paths = {
      instance.PerInstanceInternalPath("uwb_fifo_vm.in"),
      instance.PerInstanceInternalPath("uwb_fifo_vm.out"),
  };
  std::vector<SharedFD> fifos;
  for (const auto& path : fifo_paths) {
    unlink(path.c_str());
    CF_EXPECTF(mkfifo(path.c_str(), 0660) == 0, "Could not create '{}': '{}'",
               path, strerror(errno));
    auto fd = SharedFD::Open(path, O_RDWR);
    CF_EXPECTF(fd->IsOpen(), "Could not open '{}': {}", path, fd->StrError());
    fifos.push_back(fd);
  }
  return Command(HostBinaryPath("tcp_connector"))
      .AddParameter("-fifo_out=", fifos[0])
      .AddParameter("-fifo_in=", fifos[1])
      .AddParameter("-data_port=", config.pica_uci_port())
      .AddParameter("-buffer_size=", kBufferSize);
}

}  // namespace cuttlefish
