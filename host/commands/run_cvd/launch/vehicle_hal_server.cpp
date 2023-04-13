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

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class VehicleHalServer : public CommandSource {
 public:
  INJECT(VehicleHalServer(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command grpc_server(VehicleHalGrpcServerBinary());

    const unsigned vhal_server_cid = 2;
    const unsigned vhal_server_port = instance_.vehicle_hal_server_port();
    const std::string vhal_server_power_state_file =
        AbsolutePath(instance_.PerInstancePath("power_state"));
    const std::string vhal_server_power_state_socket =
        AbsolutePath(instance_.PerInstancePath("power_state_socket"));

    grpc_server.AddParameter("--server_cid=", vhal_server_cid);
    grpc_server.AddParameter("--server_port=", vhal_server_port);
    grpc_server.AddParameter("--power_state_file=",
                             vhal_server_power_state_file);
    grpc_server.AddParameter("--power_state_socket=",
                             vhal_server_power_state_socket);
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(grpc_server));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "VehicleHalServer"; }
  bool Enabled() const override {
    return instance_.enable_vehicle_hal_grpc_server() &&
           FileExists(VehicleHalGrpcServerBinary());
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
VehicleHalServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, VehicleHalServer>()
      .addMultibinding<SetupFeature, VehicleHalServer>();
}

}  // namespace cuttlefish
