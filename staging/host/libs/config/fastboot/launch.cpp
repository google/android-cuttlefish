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

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class FastbootProxy : public CommandSource {
 public:
  INJECT(FastbootProxy(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  Result<std::vector<Command>> Commands() override {
    std::vector<Command> commands;
    const std::string ethernet_host = instance_.ethernet_ipv6() + "%" +
                                      instance_.ethernet_bridge_name();

    Command tunnel(SocketVsockProxyBinary());
    tunnel.AddParameter("--server_type=", "tcp");
    tunnel.AddParameter("--server_tcp_port=", instance_.fastboot_host_port());
    tunnel.AddParameter("--client_type=", "tcp");
    tunnel.AddParameter("--client_tcp_host=", ethernet_host);
    tunnel.AddParameter("--client_tcp_port=", "5554");
    tunnel.AddParameter("--label=", "fastboot");
    commands.emplace_back(std::move(tunnel));

    return commands;
  }

  std::string Name() const override { return "FastbootProxy"; }
  bool Enabled() const override { return true; }

 private:
  bool Setup() override { return true; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
LaunchFastbootComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, FastbootProxy>()
      .addMultibinding<SetupFeature, FastbootProxy>();
}

}  // namespace cuttlefish
