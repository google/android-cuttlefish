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

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image_utils.h"

namespace cuttlefish {
namespace {

constexpr char kAvbtool[] = "avbtool";

}  // namespace

Result<std::vector<GuestInfo>> GetGuestInfo(const GuestPaths& guest_paths) {
  std::vector<GuestInfo> result;
  result.reserve(guest_paths.artifacts.size());
  int i = 1;

  for (const std::string& artifact_path : guest_paths.artifacts) {
    const std::string boot_image_path =
        fmt::format("{}/boot.img", artifact_path);
    const std::string avbtool_path =
        fmt::format("{}/bin/{}", guest_paths.host_artifacts, kAvbtool);
    result.emplace_back(GuestInfo{
        .instance_number = i,
        .os_version = CF_EXPECTF(
            ReadAndroidVersionFromBootImage(boot_image_path, avbtool_path),
            "Failed to read guest os version from \"{}\" using `{}` at "
            "\"{}\".",
            boot_image_path, kAvbtool, avbtool_path),
    });
    i++;
  }
  return result;
}

}  // namespace cuttlefish
