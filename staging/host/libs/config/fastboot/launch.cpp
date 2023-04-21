/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "host/libs/config/fastboot/fastboot.h"

#include <utility>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class FastbootProxy : public CommandSource {
 public:
  INJECT(FastbootProxy(const CuttlefishConfig::InstanceSpecific& instance,
                       const FastbootConfig& fastboot_config))
      : instance_(instance),
        fastboot_config_(fastboot_config) {}

  Result<std::vector<MonitorCommand>> Commands() override {
    const std::string ethernet_host = instance_.ethernet_ipv6() + "%" +
                                      instance_.ethernet_bridge_name();

    Command tunnel(SocketVsockProxyBinary());
    tunnel.AddParameter("--server_type=", "tcp");
    tunnel.AddParameter("--server_tcp_port=", instance_.fastboot_host_port());
    tunnel.AddParameter("--client_type=", "tcp");
    tunnel.AddParameter("--client_tcp_host=", ethernet_host);
    tunnel.AddParameter("--client_tcp_port=", "5554");
    tunnel.AddParameter("--label=", "fastboot");

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(tunnel));
    return commands;
  }

  std::string Name() const override { return "FastbootProxy"; }
  bool Enabled() const override {
    return instance_.boot_flow() == CuttlefishConfig::InstanceSpecific::BootFlow::Android &&
           fastboot_config_.ProxyFastboot();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {};
  }

  bool Setup() override {
    return true;
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  const FastbootConfig& fastboot_config_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific,
                                 const FastbootConfig>>
LaunchFastbootComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, FastbootProxy>()
      .addMultibinding<SetupFeature, FastbootProxy>();
}

}  // namespace cuttlefish
