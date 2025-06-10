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

#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"

namespace cuttlefish {

Result<void> InitializeMetadataImage(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (FileExists(instance.metadata_image()) &&
      FileSize(instance.metadata_image()) == instance.blank_metadata_image_mb()
                                                 << 20) {
    return {};
  }

  CF_EXPECT(CreateBlankImage(instance.metadata_image(),
                             instance.blank_metadata_image_mb(), "none"),
            "Failed to create \"" << instance.metadata_image()
                                  << "\" with size "
                                  << instance.blank_metadata_image_mb());
  return {};
}

}  // namespace cuttlefish
