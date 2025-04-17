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
#pragma once

#include <stdint.h>
#include <optional>
#include <string>

namespace cuttlefish {

struct IfaceData {
  std::string name;
  uint32_t session_id;
  uint32_t resource_id;
};

struct IfaceConfig {
  IfaceData mobile_tap;
  IfaceData bridged_wireless_tap;
  IfaceData non_bridged_wireless_tap;
  IfaceData ethernet_tap;
};

IfaceConfig DefaultNetworkInterfaces(int num);

// Acquires interfaces from the resource allocator daemon.
std::optional<IfaceConfig> AllocateNetworkInterfaces();

} // namespace cuttlefish
