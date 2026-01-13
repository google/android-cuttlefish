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

#include "cuttlefish/result/result.h"

namespace cuttlefish {

inline constexpr char kCvdNetworkGroupName[] = "cvdnetwork";

Result<void> AddTapIface(std::string_view name);
Result<void> ShutdownIface(std::string_view name);
Result<void> BringUpIface(std::string_view name);
Result<void> AddGateway(std::string_view name, std::string_view gateway,
                        std::string_view netmask);
Result<void> DestroyGateway(std::string_view name, std::string_view gateway,
                            std::string_view netmask);
Result<void> LinkTapToBridge(std::string_view tap_name,
                             std::string_view bridge_name);
Result<void> DeleteIface(std::string_view name);
Result<bool> BridgeExists(std::string_view name);
Result<void> CreateBridge(std::string_view name);
Result<void> IptableConfig(std::string_view network, bool add);

}  // namespace cuttlefish
