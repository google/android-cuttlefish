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

#include "cuttlefish/host/commands/assemble_cvd/disk/ap_composite_disk.h"

#include <vector>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

std::vector<ImagePartition> GetApCompositeDiskConfig(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  if (instance.ap_boot_flow() == APBootFlow::Grub) {
    partitions.push_back(ImagePartition{
        .label = "ap_esp",
        .image_file_path = AbsolutePath(instance.ap_esp_image_path()),
    });
  }

  partitions.push_back(ImagePartition{
      .label = "ap_rootfs",
      .image_file_path = AbsolutePath(config.ap_rootfs_image()),
  });

  return partitions;
}

}  // namespace cuttlefish
