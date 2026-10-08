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
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "allocd/alloc_driver.h"
#include "cuttlefish/process/execute.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> AddTapIface(absl::string_view name) {
  CF_EXPECT(Execute({"ip", "tuntap", "add", "dev", std::string(name), "mode",
                     "tap", "group", kCvdNetworkGroupName, "vnet_hdr"}) == 0,
            "AddTapIface");
  return {};
}

Result<void> ShutdownIface(absl::string_view name) {
  CF_EXPECT(
      Execute({"ip", "link", "set", "dev", std::string(name), "down"}) == 0,
      "ShutdownIface");
  return {};
}

Result<void> BringUpIface(absl::string_view name) {
  CF_EXPECT(Execute({"ip", "link", "set", "dev", std::string(name), "up"}) == 0,
            "ShutdownIface");
  return {};
}

Result<void> AddGateway(absl::string_view name, absl::string_view gateway,
                        absl::string_view netmask) {
  CF_EXPECT(
      Execute({"ip", "addr", "add", std::string(gateway) + std::string(netmask),
               "broadcast", "+", "dev", std::string(name)}) == 0,
      "AddGateway");
  return {};
}

Result<void> DestroyGateway(absl::string_view name, absl::string_view gateway,
                            absl::string_view netmask) {
  CF_EXPECT(
      Execute({"ip", "addr", "del", std::string(gateway) + std::string(netmask),
               "broadcast", "+", "dev", std::string(name)}) == 0,
      "DestroyGateway");
  return {};
}

Result<void> LinkTapToBridge(absl::string_view tap_name,
                             absl::string_view bridge_name) {
  CF_EXPECT(Execute({"ip", "link", "set", "dev", std::string(tap_name),
                     "master", std::string(bridge_name)}) == 0,
            "LinkTapToBridge");
  return {};
}

Result<void> DeleteIface(absl::string_view name) {
  CF_EXPECT(Execute({"ip", "link", "delete", std::string(name)}) == 0,
            "DeleteIface");
  return {};
}

Result<bool> BridgeExists(absl::string_view name) {
  return Execute({"ip", "link", "show", std::string(name)}) == 0;
}

Result<bool> BridgeInUse(absl::string_view name) {
  return Execute({"sh", "-c",
                  absl::StrCat("[ $(ip link show master ", name,
                               " | wc -l) -ne 0 ]")}) == 0;
}

Result<void> CreateBridge(absl::string_view name) {
  CF_EXPECT(Execute({"ip", "link", "add", "name", std::string(name), "type",
                     "bridge", "forward_delay", "0", "stp_state", "0"}) == 0,
            "CreateBridge");
  return {};
}

}  // namespace cuttlefish
