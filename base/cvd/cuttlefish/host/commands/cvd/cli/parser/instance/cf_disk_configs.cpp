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
#include <string_view>
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

using cvd::config::DiceHandover;
using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;

// The dice handover mapping is always 64K: crosvm requires the mapping to be
// page aligned, and 64K is a multiple of every supported guest page size
// (4K, 16K and 64K), so the same size works on all platforms.
inline constexpr char kDiceHandoverMappingSize[] = "0x10000";
inline constexpr std::string_view kFetchedDiceHandoverFilename =
    "dice_handover";

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

bool IsPrefixedHex(std::string_view value) {
  return value.size() > 2 && value.starts_with("0x") &&
         value.find_first_not_of("0123456789abcdefABCDEF", 2) ==
             std::string_view::npos;
}

Result<void> ValidateDiceHandover(const DiceHandover& dice_handover) {
  const std::string& build = dice_handover.build();
  CF_EXPECT(!build.empty(),
            "`disk_artifacts.dice_handover.build` must not be empty");
  CF_EXPECT(build.find(',') == std::string::npos,
            "`disk_artifacts.dice_handover.build` must not contain ','");
  CF_EXPECTF(IsPrefixedHex(dice_handover.addr()),
             "`disk_artifacts.dice_handover.addr` must be a hexadecimal "
             "number with 0x prefix, got \"{}\"",
             dice_handover.addr());
  switch (dice_handover.firmware_table_case()) {
    case DiceHandover::kAcpiTable:
      CF_EXPECT(!dice_handover.acpi_table().empty(),
                "`disk_artifacts.dice_handover.acpi_table` must not be empty");
      CF_EXPECT(
          dice_handover.acpi_table().find(',') == std::string::npos,
          "`disk_artifacts.dice_handover.acpi_table` must not contain ','");
      break;
    case DiceHandover::kDeviceTreeOverlay:
      CF_EXPECT(!dice_handover.device_tree_overlay().empty(),
                "`disk_artifacts.dice_handover.device_tree_overlay` must not "
                "be empty");
      CF_EXPECT(
          dice_handover.device_tree_overlay().find(',') == std::string::npos,
          "`disk_artifacts.dice_handover.device_tree_overlay` must not "
          "contain ','");
      break;
    case DiceHandover::FIRMWARE_TABLE_NOT_SET:
      return CF_ERR(
          "One of `disk_artifacts.dice_handover.acpi_table` or "
          "`disk_artifacts.dice_handover.device_tree_overlay` must be set");
  }
  return {};
}

Result<std::string> CrosvmAcpiTable(const Instance& instance) {
  const cvd::config::DiskArtifacts& artifacts =
      instance.disk().disk_artifacts();
  if (!artifacts.has_dice_handover()) {
    return "";
  }
  return artifacts.dice_handover().acpi_table();
}

Result<std::string> CrosvmDeviceTreeOverlay(const Instance& instance) {
  const cvd::config::DiskArtifacts& artifacts =
      instance.disk().disk_artifacts();
  if (!artifacts.has_dice_handover()) {
    return "";
  }
  return artifacts.dice_handover().device_tree_overlay();
}

// Base64-encode before GenerateVecFlag joins the per-instance values with
// commas: a file-backed mapping natively contains commas and would otherwise
// be split across instances by assemble_cvd.
Result<std::string> CrosvmFileBackedMappingBase64(const Instance& instance) {
  const cvd::config::DiskArtifacts& artifacts =
      instance.disk().disk_artifacts();
  if (!artifacts.has_dice_handover()) {
    return CF_EXPECT(EncodeBase64(""));
  }
  const DiceHandover& dice_handover = artifacts.dice_handover();
  const std::string_view path = dice_handover.build().starts_with(kFetchPrefix)
                                    ? kFetchedDiceHandoverFilename
                                    : std::string_view(dice_handover.build());
  return CF_EXPECT(EncodeBase64(fmt::format("path={},addr={},size={}", path,
                                            dice_handover.addr(),
                                            kDiceHandoverMappingSize)));
}

}  // namespace

Result<std::vector<std::string>> GenerateDiskFlags(
    const EnvironmentSpecification& config) {
  for (const Instance& instance : config.instances()) {
    if (instance.disk().disk_artifacts().has_dice_handover()) {
      CF_EXPECT(ValidateDiceHandover(
          instance.disk().disk_artifacts().dice_handover()));
    }
  }
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
                    const cvd::config::DiskArtifacts& artifacts =
                        instance.disk().disk_artifacts();
                    return artifacts.has_dice_handover() &&
                           artifacts.dice_handover().has_acpi_table();
                  })) {
    flags.emplace_back(CF_EXPECT(
        ResultInstanceFlag(kFlagCrosvmAcpiTable, config, CrosvmAcpiTable)));
  }
  if (std::any_of(config.instances().cbegin(), config.instances().cend(),
                  [](const Instance& instance) {
                    const cvd::config::DiskArtifacts& artifacts =
                        instance.disk().disk_artifacts();
                    return artifacts.has_dice_handover() &&
                           artifacts.dice_handover().has_device_tree_overlay();
                  })) {
    flags.emplace_back(CF_EXPECT(ResultInstanceFlag(
        kFlagCrosvmDeviceTreeOverlay, config, CrosvmDeviceTreeOverlay)));
  }
  if (std::any_of(config.instances().cbegin(), config.instances().cend(),
                  [](const Instance& instance) {
                    return instance.disk().disk_artifacts().has_dice_handover();
                  })) {
    flags.emplace_back(
        CF_EXPECT(ResultInstanceFlag(kFlagCrosvmFileBackedMappingBase64, config,
                                     CrosvmFileBackedMappingBase64)));
  }
  return flags;
}

}  // namespace cuttlefish
