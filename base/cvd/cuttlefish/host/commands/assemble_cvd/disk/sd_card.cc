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

#include "cuttlefish/host/commands/assemble_cvd/disk/sd_card.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

Result<void> InitializeSdCard(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (!instance.use_sdcard()) {
    return {};
  }
  if (FileExists(instance.sdcard_path())) {
    return {};
  }
  CF_EXPECT(CreateBlankImage(instance.sdcard_path(),
                             instance.blank_sdcard_image_mb(), "sdcard"),
            "Failed to create \"" << instance.sdcard_path() << "\"");
  if (config.vm_manager() == VmmMode::kQemu) {
    const std::string crosvm_path = instance.crosvm_binary();
    CreateQcowOverlay(crosvm_path, instance.sdcard_path(),
                      instance.sdcard_overlay_path());
  }
  return {};
}

}  // namespace cuttlefish
