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

#include "cuttlefish/host/libs/metrics/guest_metrics.h"

#include <string>
#include <vector>

#include <fmt/format.h>

#include "cuttlefish/host/commands/assemble_cvd/boot_image_utils.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kAvbtool[] = "avbtool";

}  // namespace

Result<std::vector<GuestMetrics>> GetGuestMetrics(const Guests& guests) {
  std::vector<GuestMetrics> result;
  result.reserve(guests.guest_infos.size());

  for (const GuestInfo& guest : guests.guest_infos) {
    const std::string boot_image_path =
        fmt::format("{}/boot.img", guest.product_out);
    const std::string avbtool_path =
        fmt::format("{}/bin/{}", guests.host_artifacts, kAvbtool);
    result.emplace_back(GuestMetrics{
        .instance_id = guest.instance_id,
        .os_version = CF_EXPECTF(
            ReadAndroidVersionFromBootImage(boot_image_path, avbtool_path),
            "Failed to read guest os version from \"{}\" using `{}` at "
            "\"{}\".",
            boot_image_path, kAvbtool, avbtool_path),
    });
  }
  return result;
}

}  // namespace cuttlefish
