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

#include "absl/strings/string_view.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

inline constexpr char kCvdNetworkGroupName[] = "cvdnetwork";

Result<void> AddTapIface(absl::string_view name);
Result<void> ShutdownIface(absl::string_view name);
Result<void> BringUpIface(absl::string_view name);
Result<void> AddGateway(absl::string_view name, absl::string_view gateway,
                        absl::string_view netmask);
Result<void> DestroyGateway(absl::string_view name, absl::string_view gateway,
                            absl::string_view netmask);
Result<void> LinkTapToBridge(absl::string_view tap_name,
                             absl::string_view bridge_name);
Result<void> DeleteIface(absl::string_view name);
Result<bool> BridgeInUse(absl::string_view name);
Result<bool> BridgeExists(absl::string_view name);
Result<void> CreateBridge(absl::string_view name);

}  // namespace cuttlefish
