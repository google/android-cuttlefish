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

#include "cuttlefish/host/commands/assemble_cvd/disk/linux_composite_disk.h"

#include <vector>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/gpt_type_guid.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

std::vector<ImagePartition> LinuxCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  partitions.push_back(ImagePartition{
      .label = "linux_esp",
      .image_file_path = AbsolutePath(instance.esp_image_path()),
      .type = GptPartitionType::kEfiSystemPartition,
  });
  partitions.push_back(ImagePartition{
      .label = "linux_root",
      .image_file_path = AbsolutePath(instance.linux_root_image()),
  });

  return partitions;
}

}  // namespace cuttlefish
