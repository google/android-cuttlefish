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

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> AddTapIface(std::string_view name) {
  CF_EXPECT(Execute({"ip", "tuntap", "add", "dev", std::string(name), "mode",
                     "tap", "group", kCvdNetworkGroupName, "vnet_hdr"}) == 0,
            "AddTapIface");
  return {};
}

Result<void> ShutdownIface(std::string_view name) {
  CF_EXPECT(
      Execute({"ip", "link", "set", "dev", std::string(name), "down"}) == 0,
      "ShutdownIface");
  return {};
}

Result<void> BringUpIface(std::string_view name) {
  CF_EXPECT(Execute({"ip", "link", "set", "dev", std::string(name), "up"}) == 0,
            "ShutdownIface");
  return {};
}

Result<void> AddGateway(std::string_view name, std::string_view gateway,
                        std::string_view netmask) {
  CF_EXPECT(
      Execute({"ip", "addr", "add", std::string(gateway) + std::string(netmask),
               "broadcast", "+", "dev", std::string(name)}) == 0,
      "AddGateway");
  return {};
}

Result<void> DestroyGateway(std::string_view name, std::string_view gateway,
                            std::string_view netmask) {
  CF_EXPECT(
      Execute({"ip", "addr", "del", std::string(gateway) + std::string(netmask),
               "broadcast", "+", "dev", std::string(name)}) == 0,
      "DestroyGateway");
  return {};
}

Result<void> LinkTapToBridge(std::string_view tap_name,
                             std::string_view bridge_name) {
  CF_EXPECT(Execute({"ip", "link", "set", "dev", std::string(tap_name),
                     "master", std::string(bridge_name)}) == 0,
            "LinkTapToBridge");
  return {};
}

Result<void> DeleteIface(std::string_view name) {
  CF_EXPECT(Execute({"ip", "link", "delete", std::string(name)}) == 0,
            "DeleteIface");
  return {};
}

Result<bool> BridgeExists(std::string_view name) {
  return Execute({"ip", "link", "show", std::string(name)}) == 0;
}

Result<bool> BridgeInUse(std::string_view name) {
  return false;
}

Result<void> CreateBridge(std::string_view name) {
  CF_EXPECT(Execute({"ip", "link", "add", "name", std::string(name), "type",
                     "bridge", "forward_delay", "0", "stp_state", "0"}) == 0,
            "CreateBridge");
  return {};
}

Result<void> IptableConfig(std::string_view network, bool add) {
  CF_EXPECT(Execute({"iptables", "-t", "nat", add ? "-A" : "-D", "POSTROUTING",
                     "-s", std::string(network), "-j", "MASQUERADE"}) == 0,
            "IptableConfig");
  return {};
}

}  // namespace cuttlefish
