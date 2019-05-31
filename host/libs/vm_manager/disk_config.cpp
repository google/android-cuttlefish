/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/libs/vm_manager/disk_config.h"

#include <vector>

#include <glog/logging.h>

#include "common/libs/utils/files.h"
#include "host/libs/vm_manager/image_aggregator.h"
#include "host/libs/config/cuttlefish_config.h"

namespace vm_manager {

namespace {

std::vector<ImagePartition> disk_config(const vsoc::CuttlefishConfig& config) {
  std::vector<ImagePartition> partitions;
  if (config.super_image_path().empty()) {
    partitions.push_back(ImagePartition {
      .label = "system",
      .image_file_path = config.system_image_path(),
    });
  } else {
    partitions.push_back(ImagePartition {
      .label = "super",
      .image_file_path = config.super_image_path(),
    });
  }
  partitions.push_back(ImagePartition {
    .label = "userdata",
    .image_file_path = config.data_image_path(),
  });
  partitions.push_back(ImagePartition {
    .label = "cache",
    .image_file_path = config.cache_image_path(),
  });
  partitions.push_back(ImagePartition {
    .label = "metadata",
    .image_file_path = config.metadata_image_path(),
  });
  if (config.super_image_path().empty()) {
    partitions.push_back(ImagePartition {
      .label = "product",
      .image_file_path = config.product_image_path(),
    });
    partitions.push_back(ImagePartition {
      .label = "vendor",
      .image_file_path = config.vendor_image_path(),
    });
  }
  return partitions;
}

} // namespace

bool should_create_composite_disk(const vsoc::CuttlefishConfig& config) {
  if (config.composite_disk_path().empty()) {
    return false;
  }
  auto composite_age = cvd::FileModificationTime(config.composite_disk_path());
  for (auto& partition : disk_config(config)) {
    auto partition_age = cvd::FileModificationTime(partition.image_file_path);
    if (partition_age >= composite_age) {
      LOG(INFO) << "composite disk age was \"" << std::chrono::system_clock::to_time_t(composite_age) << "\", "
                << "partition age was \"" << std::chrono::system_clock::to_time_t(partition_age) << "\"";
      return true;
    }
  }
  return false;
}

void create_composite_disk(const vsoc::CuttlefishConfig& config) {
  if (config.composite_disk_path().empty()) {
    LOG(FATAL) << "asked to create composite disk, but path was empty";
  }
  aggregate_image(disk_config(config), config.composite_disk_path());
}

} // namespace vm_manager
