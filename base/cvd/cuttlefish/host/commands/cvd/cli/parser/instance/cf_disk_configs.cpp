/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include "cuttlefish/host/commands/cvd/cli/parser/instance/cf_disk_configs.h"

#include <algorithm>
#include <string>
#include <vector>

#include "fmt/format.h"

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/libs/config/data_image_policy.h"
#include "cuttlefish/result/result.h"

#define DEFAULT_BLANK_DATA_IMAGE_SIZE "unset"

namespace cuttlefish {
namespace {

using cvd::config::DeviceTreeOverlayArtifact;
using cvd::config::EnvironmentSpecification;
using cvd::config::FileBackedMapping;
using cvd::config::Instance;

inline constexpr char kFlagCrosvmAcpiTable[] = "crosvm_acpi_table";
inline constexpr char kFlagCrosvmDeviceTreeOverlay[] =
    "crosvm_device_tree_overlay";
inline constexpr char kFlagCrosvmFileBackedMappingBase64[] =
    "crosvm_file_backed_mapping_base64";

std::string VendorBootImageFlag(const Instance& instance) {
  const std::string& vendor_boot =
      instance.disk().disk_artifacts().vendor_boot();
  if (!vendor_boot.starts_with(kFetchPrefix)) {
    return vendor_boot;
  }
  return "";
}

Result<std::string> CrosvmAcpiTable(const Instance& instance) {
  if (!instance.disk().disk_artifacts().has_acpi_table()) {
    return "";
  }
  const std::string& acpi_table = instance.disk().disk_artifacts().acpi_table();
  CF_EXPECT(!acpi_table.empty(),
            "`disk_artifacts.acpi_table` must not be empty");
  return acpi_table;
}

Result<std::string> CrosvmDeviceTreeOverlay(const Instance& instance) {
  if (!instance.disk().disk_artifacts().has_device_tree_overlay()) {
    return "";
  }
  const DeviceTreeOverlayArtifact& overlay =
      instance.disk().disk_artifacts().device_tree_overlay();
  CF_EXPECT(!overlay.build().empty(),
            "`disk_artifacts.device_tree_overlay.build` must not be empty");
  return overlay.build();
}

// Base64-encode before GenerateVecFlag joins the per-instance values with
// commas: a file-backed mapping natively contains commas and would otherwise
// be split across instances by assemble_cvd.
Result<std::string> CrosvmFileBackedMappingBase64(const Instance& instance) {
  if (!instance.disk().disk_artifacts().has_file_backed_mapping()) {
    return CF_EXPECT(EncodeBase64(""));
  }
  const FileBackedMapping& mapping =
      instance.disk().disk_artifacts().file_backed_mapping();
  CF_EXPECT(!mapping.build().empty(),
            "`disk_artifacts.file_backed_mapping.build` must not be empty");
  CF_EXPECT(mapping.build().find(',') == std::string::npos,
            "`disk_artifacts.file_backed_mapping.build` must not contain ','");
  CF_EXPECT(!mapping.addr().empty(),
            "`disk_artifacts.file_backed_mapping.addr` must not be empty");
  CF_EXPECT(mapping.addr().find(',') == std::string::npos,
            "`disk_artifacts.file_backed_mapping.addr` must not contain ','");
  CF_EXPECT(!mapping.size().empty(),
            "`disk_artifacts.file_backed_mapping.size` must not be empty");
  CF_EXPECT(mapping.size().find(',') == std::string::npos,
            "`disk_artifacts.file_backed_mapping.size` must not contain ','");
  return CF_EXPECT(
      EncodeBase64(fmt::format("path={},addr={},size={}", mapping.build(),
                               mapping.addr(), mapping.size())));
}

}  // namespace

Result<std::vector<std::string>> GenerateDiskFlags(
    const EnvironmentSpecification& config) {
  std::vector<std::string> flags;
  if (std::any_of(config.instances().cbegin(), config.instances().cend(),
                  [](const Instance& instance) {
                    return instance.disk().has_blank_data_image_mb();
                  })) {
    std::vector<std::string> data_image_mbs;
    std::vector<std::string> data_policies;
    for (const Instance& instance : config.instances()) {
      const cvd::config::Disk& disk = instance.disk();
      if (disk.has_blank_data_image_mb()) {
        data_image_mbs.emplace_back(std::to_string(disk.blank_data_image_mb()));
        data_policies.emplace_back(
            DataImagePolicyString(DataImagePolicy::AlwaysCreate));
      } else {
        data_image_mbs.emplace_back(DEFAULT_BLANK_DATA_IMAGE_SIZE);
        data_policies.emplace_back(
            DataImagePolicyString(DataImagePolicy::UseExisting));
      }
    }
    flags.emplace_back(GenerateVecFlag("blank_data_image_mb", data_image_mbs));
    flags.emplace_back(GenerateVecFlag("data_policy", data_policies));
  }
  if (std::any_of(config.instances().cbegin(), config.instances().cend(),
                  [](const Instance& instance) {
                    return !VendorBootImageFlag(instance).empty();
                  })) {
    flags.emplace_back(
        GenerateInstanceFlag("vendor_boot_image", config, VendorBootImageFlag));
  }
  if (std::any_of(config.instances().cbegin(), config.instances().cend(),
                  [](const Instance& instance) {
                    return instance.disk().disk_artifacts().has_acpi_table();
                  })) {
    flags.emplace_back(CF_EXPECT(
        ResultInstanceFlag(kFlagCrosvmAcpiTable, config, CrosvmAcpiTable)));
  }
  if (std::any_of(
          config.instances().cbegin(), config.instances().cend(),
          [](const Instance& instance) {
            return instance.disk().disk_artifacts().has_device_tree_overlay();
          })) {
    flags.emplace_back(CF_EXPECT(ResultInstanceFlag(
        kFlagCrosvmDeviceTreeOverlay, config, CrosvmDeviceTreeOverlay)));
  }
  if (std::any_of(
          config.instances().cbegin(), config.instances().cend(),
          [](const Instance& instance) {
            return instance.disk().disk_artifacts().has_file_backed_mapping();
          })) {
    flags.emplace_back(
        CF_EXPECT(ResultInstanceFlag(kFlagCrosvmFileBackedMappingBase64, config,
                                     CrosvmFileBackedMappingBase64)));
  }
  return flags;
}

}  // namespace cuttlefish
