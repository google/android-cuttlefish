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

#include "cuttlefish/host/commands/assemble_cvd/disk/vbmeta_enforce_minimum_size.h"

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> VbmetaEnforceMinimumSize(
    const CuttlefishConfig::InstanceSpecific& instance,
    AndroidBuild& android_build) {
  const std::set<std::string, std::less<void>> logical_partitions =
      CF_EXPECT(android_build.LogicalPartitions());

  // libavb expects to be able to read the maximum vbmeta size, so we must
  // provide a partition which matches this or the read will fail. Skip base
  // vbmeta images in system_image_dir when they are logical partitions inside
  // super.img so read-only build artifact directories are never mutated.
  const std::pair<std::string_view, std::string> base_vbmetas[] = {
      {"vbmeta", instance.vbmeta_image()},
      {"vbmeta_system", instance.vbmeta_system_image()},
      {"vbmeta_vendor_dlkm", instance.vbmeta_vendor_dlkm_image()},
      {"vbmeta_system_dlkm", instance.vbmeta_system_dlkm_image()},
  };
  for (const auto& [name, vbmeta_image] : base_vbmetas) {
    if (logical_partitions.count(name) > 0) {
      continue;
    }
    if (!vbmeta_image.empty() && FileExists(vbmeta_image)) {
      CF_EXPECT(EnforceVbMetaSize(vbmeta_image));
    }
  }
  for (const auto& new_vbmeta :
       {instance.new_vbmeta_image(), instance.new_vbmeta_vendor_dlkm_image(),
        instance.new_vbmeta_system_dlkm_image()}) {
    if (!new_vbmeta.empty() && FileExists(new_vbmeta)) {
      CF_EXPECT(EnforceVbMetaSize(new_vbmeta));
    }
  }
  return {};
}

}  // namespace cuttlefish
