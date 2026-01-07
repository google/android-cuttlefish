/*
 * Copyright (C) 2020 The Android Open Source Project
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
#include "allocd/alloc_driver.h"

#include <cstdint>
#include <fstream>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

namespace cuttlefish {

extern int RunExternalCommand(const std::string& name);

bool AddTapIface(std::string_view name) {
  std::stringstream ss;
  ss << "ip tuntap add dev " << name << " mode tap group cvdnetwork vnet_hdr";
  auto add_command = ss.str();
  LOG(INFO) << "Create tap interface: " << add_command;
  int status = RunExternalCommand(add_command);
  return status == 0;
}

bool ShutdownIface(std::string_view name) {
  std::stringstream ss;
  ss << "ip link set dev " << name << " down";
  auto link_command = ss.str();
  LOG(INFO) << "Shutdown tap interface: " << link_command;
  int status = RunExternalCommand(link_command);

  return status == 0;
}

bool BringUpIface(std::string_view name) {
  std::stringstream ss;
  ss << "ip link set dev " << name << " up";
  auto link_command = ss.str();
  LOG(INFO) << "Bring up tap interface: " << link_command;
  int status = RunExternalCommand(link_command);

  return status == 0;
}

bool AddGateway(std::string_view name, std::string_view gateway,
                std::string_view netmask) {
  std::stringstream ss;
  ss << "ip addr add " << gateway << netmask << " broadcast + dev " << name;
  auto command = ss.str();
  LOG(INFO) << "setup gateway: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

bool DestroyGateway(std::string_view name, std::string_view gateway,
                    std::string_view netmask) {
  std::stringstream ss;
  ss << "ip addr del " << gateway << netmask << " broadcast + dev " << name;
  auto command = ss.str();
  LOG(INFO) << "removing gateway: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

bool LinkTapToBridge(std::string_view tap_name,
                     std::string_view bridge_name) {
  std::stringstream ss;
  ss << "ip link set dev " << tap_name << " master " << bridge_name;
  auto command = ss.str();
  int status = RunExternalCommand(command);

  return status == 0;
}

bool DeleteIface(std::string_view name) {
  std::stringstream ss;
  ss << "ip link delete " << name;
  auto link_command = ss.str();
  LOG(INFO) << "Delete tap interface: " << link_command;
  int status = RunExternalCommand(link_command);

  return status == 0;
}

bool BridgeExists(std::string_view name) {
  std::stringstream ss;
  ss << "ip link show " << name << " >/dev/null";

  auto command = ss.str();
  LOG(INFO) << "bridge exists: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

bool CreateBridge(std::string_view name) {
  std::stringstream ss;
  ss << "ip link add name " << name
     << " type bridge forward_delay 0 stp_state 0";

  auto command = ss.str();
  LOG(INFO) << "create bridge: " << command;
  int status = RunExternalCommand(command);

  if (status != 0) {
    return false;
  }

  return BringUpIface(name);
}

bool IptableConfig(std::string_view network, bool add) {
  std::stringstream ss;
  ss << "iptables -t nat " << (add ? "-A" : "-D") << " POSTROUTING -s "
     << network << " -j MASQUERADE";

  auto command = ss.str();
  LOG(INFO) << "iptable_config: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

}  // namespace cuttlefish
