/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/disk/android_composite_disk_config.h"

#include <sys/statvfs.h>

#include <optional>
#include <string>
#include <vector>

#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {
namespace {

std::optional<ImagePartition> HibernationImage(
    const SystemImageDirFlag& system_image_dir,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string path = AbsolutePath(system_image_dir.ForIndex(instance.index()) +
                                  "/hibernation_swap.img");
  ImagePartition image = ImagePartition{
      .label = "hibernation",
      .image_file_path = path,
  };
  return FileExists(path) ? std::optional<ImagePartition>(image) : std::nullopt;
}

}  // namespace

std::vector<ImagePartition> AndroidCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance,
    const MetadataImage& metadata_image, const MiscImage& misc_image,
    const SystemImageDirFlag& system_image_dir) {
  std::vector<ImagePartition> partitions;

  partitions.push_back(misc_image.Partition());
  partitions.push_back(ImagePartition{
      .label = "boot_a",
      .image_file_path = AbsolutePath(instance.new_boot_image()),
  });
  partitions.push_back(ImagePartition{
      .label = "boot_b",
      .image_file_path = AbsolutePath(instance.new_boot_image()),
  });
  const auto init_boot_path = instance.init_boot_image();
  if (FileExists(init_boot_path)) {
    partitions.push_back(ImagePartition{
        .label = "init_boot_a",
        .image_file_path = AbsolutePath(init_boot_path),
    });
    partitions.push_back(ImagePartition{
        .label = "init_boot_b",
        .image_file_path = AbsolutePath(init_boot_path),
    });
  }
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_a",
      .image_file_path = AbsolutePath(instance.new_vendor_boot_image()),
  });
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_b",
      .image_file_path = AbsolutePath(instance.new_vendor_boot_image()),
  });
  auto vbmeta_image = instance.new_vbmeta_image();
  if (!FileExists(vbmeta_image)) {
    vbmeta_image = instance.vbmeta_image();
  }
  partitions.push_back(ImagePartition{
      .label = "vbmeta_a",
      .image_file_path = AbsolutePath(vbmeta_image),
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_b",
      .image_file_path = AbsolutePath(vbmeta_image),
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_system_a",
      .image_file_path = AbsolutePath(instance.vbmeta_system_image()),
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_system_b",
      .image_file_path = AbsolutePath(instance.vbmeta_system_image()),
  });
  auto vbmeta_vendor_dlkm_img = instance.new_vbmeta_vendor_dlkm_image();
  if (!FileExists(vbmeta_vendor_dlkm_img)) {
    vbmeta_vendor_dlkm_img = instance.vbmeta_vendor_dlkm_image();
  }
  if (FileExists(vbmeta_vendor_dlkm_img)) {
    partitions.push_back(ImagePartition{
        .label = "vbmeta_vendor_dlkm_a",
        .image_file_path = AbsolutePath(vbmeta_vendor_dlkm_img),
    });
    partitions.push_back(ImagePartition{
        .label = "vbmeta_vendor_dlkm_b",
        .image_file_path = AbsolutePath(vbmeta_vendor_dlkm_img),
    });
  }
  auto vbmeta_system_dlkm_img = instance.new_vbmeta_system_dlkm_image();
  if (!FileExists(vbmeta_system_dlkm_img)) {
    vbmeta_system_dlkm_img = instance.vbmeta_system_dlkm_image();
  }
  if (FileExists(vbmeta_system_dlkm_img)) {
    partitions.push_back(ImagePartition{
        .label = "vbmeta_system_dlkm_a",
        .image_file_path = AbsolutePath(vbmeta_system_dlkm_img),
    });
    partitions.push_back(ImagePartition{
        .label = "vbmeta_system_dlkm_b",
        .image_file_path = AbsolutePath(vbmeta_system_dlkm_img),
    });
  }
  auto super_image = instance.new_super_image();
  if (!FileExists(super_image)) {
    super_image = instance.super_image();
  }
  partitions.push_back(ImagePartition{
      .label = "super",
      .image_file_path = AbsolutePath(super_image),
  });
  auto data_image = instance.new_data_image();
  if (!FileExists(data_image)) {
    data_image = instance.data_image();
  }
  partitions.push_back(ImagePartition{
      .label = "userdata",
      .image_file_path = AbsolutePath(data_image),
  });

  partitions.push_back(metadata_image.Partition());

  std::optional<ImagePartition> hibernation_partition =
      HibernationImage(system_image_dir, instance);
  if (hibernation_partition) {
    partitions.push_back(std::move(*hibernation_partition));
  }

  const auto vvmtruststore_path = instance.vvmtruststore_path();
  if (!vvmtruststore_path.empty()) {
    partitions.push_back(ImagePartition{
        .label = "vvmtruststore",
        .image_file_path = AbsolutePath(vvmtruststore_path),
    });
  }

  const auto custom_partition_path = instance.custom_partition_path();
  if (!custom_partition_path.empty()) {
    auto custom_partition_paths =
        android::base::Split(custom_partition_path, ";");
    for (int i = 0; i < custom_partition_paths.size(); i++) {
      partitions.push_back(ImagePartition{
          .label = i > 0 ? "custom_" + std::to_string(i) : "custom",
          .image_file_path = AbsolutePath(custom_partition_paths[i]),
      });
    }
  }

  return partitions;
}

}  // namespace cuttlefish
