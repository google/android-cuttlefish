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
#include <cstdint>
#include <string>
#include <string_view>

namespace cuttlefish {

inline constexpr char kCvdNetworkGroupName[] = "cvdnetwork";

bool AddTapIface(std::string_view name);
bool ShutdownIface(std::string_view name);
bool BringUpIface(std::string_view name);
bool AddGateway(std::string_view name, std::string_view gateway,
                std::string_view netmask);
bool DestroyGateway(std::string_view name, std::string_view gateway,
                    std::string_view netmask);
bool LinkTapToBridge(std::string_view tap_name, std::string_view bridge_name);
bool DeleteIface(std::string_view name);
bool BridgeExists(std::string_view name);
bool CreateBridge(std::string_view name);
bool IptableConfig(std::string_view network, bool add);

}  // namespace cuttlefish
