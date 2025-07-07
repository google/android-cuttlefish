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

#include "cuttlefish/host/commands/assemble_cvd/disk/android_efi_loader_composite_disk.h"

#include <vector>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/android_composite_disk_config.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

std::vector<ImagePartition> AndroidEfiLoaderCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance,
    const MetadataImage& metadata, const MiscImage& misc,
    const SystemImageDirFlag& system_image_dir) {
  std::vector<ImagePartition> partitions =
      AndroidCompositeDiskConfig(instance, metadata, misc, system_image_dir);
  // Cuttlefish uboot EFI bootflow by default looks at the first partition
  // for EFI application. Thus we put "android_esp" at the beginning.
  partitions.insert(
      partitions.begin(),
      ImagePartition{
          .label = "android_esp",
          .image_file_path = AbsolutePath(instance.esp_image_path()),
          .type = kEfiSystemPartition,
      });

  return partitions;
}

}  // namespace cuttlefish
