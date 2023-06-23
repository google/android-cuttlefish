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

#include "wmediumd_server.h"

#include "host/commands/run_cvd/launch/launch.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {
namespace {

// SetupFeature class for waiting wmediumd server to be settled.
// This class is used by the instance that does not launches wmediumd.
// TODO(b/276832089) remove this when run_env implementation is completed.
class ValidateWmediumdService : public SetupFeature {
 public:
  INJECT(ValidateWmediumdService(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}
  std::string Name() const override { return "ValidateWmediumdService"; }
  bool Enabled() const override {
    return config_.virtio_mac80211_hwsim() && !instance_.start_wmediumd();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (!config_.wmediumd_api_server_socket().empty()) {
      CF_EXPECT(WaitForUnixSocket(config_.wmediumd_api_server_socket(), 30));
    }
    CF_EXPECT(WaitForUnixSocket(config_.vhost_user_mac80211_hwsim(), 30));

    return {};
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

}  // namespace

WmediumdServer::WmediumdServer(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance, LogTeeCreator& log_tee,
    GrpcSocketCreator& grpc_socket)
    : config_(config),
      instance_(instance),
      log_tee_(log_tee),
      grpc_socket_(grpc_socket) {}

Result<std::vector<MonitorCommand>> WmediumdServer::Commands() {
  Command cmd(WmediumdBinary());
  cmd.AddParameter("-u", config_.vhost_user_mac80211_hwsim());
  cmd.AddParameter("-a", config_.wmediumd_api_server_socket());
  cmd.AddParameter("-c", config_path_);

  cmd.AddParameter("--grpc_uds_path=", grpc_socket_.CreateGrpcSocket(Name()));

  std::vector<MonitorCommand> commands;
  commands.emplace_back(std::move(log_tee_.CreateLogTee(cmd, "wmediumd")));
  commands.emplace_back(std::move(cmd));
  return commands;
}

std::string WmediumdServer::Name() const { return "WmediumdServer"; }

bool WmediumdServer::Enabled() const { return instance_.start_wmediumd(); }

Result<void> WmediumdServer::WaitForAvailability() const {
  if (Enabled()) {
    if (!config_.wmediumd_api_server_socket().empty()) {
      CF_EXPECT(WaitForUnixSocket(config_.wmediumd_api_server_socket(), 30));
    }
    CF_EXPECT(WaitForUnixSocket(config_.vhost_user_mac80211_hwsim(), 30));
  }

  return {};
}

std::unordered_set<SetupFeature*> WmediumdServer::Dependencies() const {
  return {};
}

Result<void> WmediumdServer::ResultSetup() {
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

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific,
                                 LogTeeCreator, GrpcSocketCreator>>
WmediumdServerComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, WmediumdServer>()
      .addMultibinding<CommandSource, WmediumdServer>()
      .addMultibinding<SetupFeature, WmediumdServer>()
      .addMultibinding<SetupFeature, ValidateWmediumdService>();
}

}  // namespace cuttlefish
