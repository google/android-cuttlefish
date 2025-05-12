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

#include "cuttlefish/host/commands/run_cvd/launch/openwrt_control_server.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"

namespace cuttlefish {
namespace {

class OpenwrtControlServer : public CommandSource {
 public:
  INJECT(OpenwrtControlServer(
      const CuttlefishConfig& config,
      const CuttlefishConfig::EnvironmentSpecific& environment,
      GrpcSocketCreator& grpc_socket))
      : config_(config), environment_(environment), grpc_socket_(grpc_socket) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    // TODO(b/288987294) Remove dependency to first_instance config when moving
    // OpenWrt to run_env is completed.
    auto first_instance = config_.Instances()[0];

    Command openwrt_control_server_cmd(OpenwrtControlServerBinary());
    openwrt_control_server_cmd.AddParameter(
        "--grpc_uds_path=", grpc_socket_.CreateGrpcSocket(Name()));
    openwrt_control_server_cmd.AddParameter(
        "--bridged_wifi_tap=",
        std::to_string(first_instance.use_bridged_wifi_tap()));
    openwrt_control_server_cmd.AddParameter("--webrtc_device_id=",
                                            first_instance.webrtc_device_id());
    openwrt_control_server_cmd.AddParameter("--launcher_log_path=",
                                            first_instance.launcher_log_path());
    openwrt_control_server_cmd.AddParameter(
        "--openwrt_log_path=",
        AbsolutePath(first_instance.PerInstanceLogPath("crosvm_openwrt.log")));

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(openwrt_control_server_cmd));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "OpenwrtControlServer"; }
  bool Enabled() const override { return environment_.enable_wifi(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override { return {}; }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::EnvironmentSpecific& environment_;
  GrpcSocketCreator& grpc_socket_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::EnvironmentSpecific,
                                 GrpcSocketCreator>>
OpenwrtControlServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, OpenwrtControlServer>()
      .addMultibinding<SetupFeature, OpenwrtControlServer>();
}

}  // namespace cuttlefish
