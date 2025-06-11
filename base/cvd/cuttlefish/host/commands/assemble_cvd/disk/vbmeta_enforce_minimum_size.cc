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

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

Result<void> VbmetaEnforceMinimumSize(
    const CuttlefishConfig::InstanceSpecific& instance) {
  // libavb expects to be able to read the maximum vbmeta size, so we must
  // provide a partition which matches this or the read will fail
  for (const auto& vbmeta_image :
       {instance.vbmeta_image(), instance.new_vbmeta_image(),
        instance.vbmeta_system_image(), instance.vbmeta_vendor_dlkm_image(),
        instance.vbmeta_system_dlkm_image()}) {
    // In some configurations of cuttlefish, the vendor dlkm vbmeta image does
    // not exist
    if (FileExists(vbmeta_image)) {
      CF_EXPECT(EnforceVbMetaSize(vbmeta_image));
    }
  }
  return {};
}

}  // namespace cuttlefish
