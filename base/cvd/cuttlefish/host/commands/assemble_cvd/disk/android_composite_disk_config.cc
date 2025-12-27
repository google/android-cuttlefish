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

#include "absl/strings/str_cat.h"
#include "android-base/strings.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Defined as constants to avoid typos in repeated names
constexpr struct {
  std::string_view boot = "boot";
  std::string_view hibernation = "hibernation";
  std::string_view init_boot = "init_boot";
  std::string_view metadata = "metadata";
  std::string_view misc = "misc";
  std::string_view super = "super";
  std::string_view userdata = "userdata";
  std::string_view vbmeta = "vbmeta";
  std::string_view vbmeta_system = "vbmeta_system";
  std::string_view vbmeta_system_dlkm = "vbmeta_system_dlkm";
  std::string_view vbmeta_vendor_dlkm = "vbmeta_vendor_dlkm";
  std::string_view vendor_boot = "vendor_boot";
  std::string_view vvmtruststore = "vvmtruststore";
} kPartitions;

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

Result<std::vector<ImagePartition>> AndroidCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance,
    const MetadataImage& metadata_image, const MiscImage& misc_image,
    const SystemImageDirFlag& system_image_dir) {
  std::vector<ImagePartition> partitions;

  const std::set<std::string_view> ab_partitions = {
      kPartitions.boot,
      kPartitions.init_boot,
      kPartitions.vbmeta,
      kPartitions.vbmeta_system,
      kPartitions.vbmeta_system_dlkm,
      kPartitions.vbmeta_vendor_dlkm,
      kPartitions.vendor_boot,
  };

  const std::set<std::string_view> optional_partitions = {
      kPartitions.init_boot,          kPartitions.vbmeta_vendor_dlkm,
      kPartitions.vbmeta_system_dlkm, kPartitions.hibernation,
      kPartitions.vvmtruststore,
  };

  const std::map<std::string_view, std::string> primary_paths = {
      {kPartitions.boot, instance.new_boot_image()},
      {kPartitions.init_boot, instance.init_boot_image()},
      {kPartitions.metadata, CF_EXPECT(metadata_image.Path())},
      {kPartitions.misc, CF_EXPECT(misc_image.Path())},
      {kPartitions.super, instance.new_super_image()},
      {kPartitions.userdata, instance.new_data_image()},
      {kPartitions.vbmeta, instance.new_vbmeta_image()},
      {kPartitions.vbmeta_system, instance.vbmeta_system_image()},
      {kPartitions.vbmeta_system_dlkm, instance.new_vbmeta_system_dlkm_image()},
      {kPartitions.vbmeta_vendor_dlkm, instance.new_vbmeta_vendor_dlkm_image()},
      {kPartitions.vendor_boot, instance.new_vendor_boot_image()},
      {kPartitions.vvmtruststore, instance.vvmtruststore_path()},
  };

  const std::map<std::string_view, std::string> fallback_paths = {
      {kPartitions.super, instance.super_image()},
      {kPartitions.vbmeta, instance.vbmeta_image()},
      {kPartitions.vbmeta_vendor_dlkm, instance.vbmeta_vendor_dlkm_image()},
      {kPartitions.vbmeta_system_dlkm, instance.vbmeta_system_dlkm_image()},
      {kPartitions.userdata, instance.data_image()},
  };

  for (const auto& [partition, path] : primary_paths) {
    std::string path_used;
    if (FileExists(path)) {
      path_used = path;
    } else if (auto it = fallback_paths.find(partition);
               it != fallback_paths.end() && FileExists(it->second)) {
      path_used = it->second;
    } else if (optional_partitions.count(partition)) {
      continue;
    } else {
      return CF_ERRF("Could not find file for partition '{}'", partition);
    }

    if (ab_partitions.count(partition)) {
      partitions.push_back(ImagePartition{
          .label = absl::StrCat(partition, "_a"),
          .image_file_path = AbsolutePath(path_used),
      });
      partitions.push_back(ImagePartition{
          .label = absl::StrCat(partition, "_b"),
          .image_file_path = AbsolutePath(path_used),
      });
    } else {
      partitions.push_back(ImagePartition{
          .label = std::string(partition),
          .image_file_path = AbsolutePath(path_used),
      });
    }
  }

  std::optional<ImagePartition> hibernation_partition =
      HibernationImage(system_image_dir, instance);
  if (hibernation_partition) {
    partitions.push_back(std::move(*hibernation_partition));
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
