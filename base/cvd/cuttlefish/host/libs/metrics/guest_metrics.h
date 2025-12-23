/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct GuestInfo {
  uint32_t instance_id;
  std::string product_out;
};

struct Guests {
  std::string host_artifacts;
  std::vector<GuestInfo> guest_infos;
};

struct GuestMetrics {
  uint32_t instance_id;
  std::string os_version;
};

Result<std::vector<GuestMetrics>> GetGuestMetrics(const Guests& guests);

}  // namespace cuttlefish
