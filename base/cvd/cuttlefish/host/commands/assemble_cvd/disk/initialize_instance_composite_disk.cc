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

#include "cuttlefish/host/commands/assemble_cvd/disk/initialize_instance_composite_disk.h"

#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/factory_reset_protected.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_vbmeta.h"
#include "cuttlefish/host/commands/assemble_cvd/disk_builder.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {
namespace {

std::vector<ImagePartition> PersistentCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance,
    const std::optional<BootConfigPartition>& bootconfig_partition,
    const FactoryResetProtectedImage& frp,
    const PersistentVbmeta& persistent_vbmeta) {
  std::vector<ImagePartition> partitions;

  // Note that if the position of uboot_env changes, the environment for
  // u-boot must be updated as well (see boot_config.cc and
  // cuttlefish.fragment in external/u-boot).
  partitions.push_back(ImagePartition{
      .label = "uboot_env",
      .image_file_path = AbsolutePath(instance.uboot_env_image_path()),
  });
  partitions.push_back(persistent_vbmeta.Partition());
  partitions.push_back(frp.Partition());
  if (bootconfig_partition) {
    partitions.push_back(bootconfig_partition->Partition());
  }
  return partitions;
}

std::vector<ImagePartition> PersistentAPCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance,
    const ApPersistentVbmeta& ap_persistent_vbmeta) {
  std::vector<ImagePartition> partitions;

  // Note that if the position of uboot_env changes, the environment for
  // u-boot must be updated as well (see boot_config.cc and
  // cuttlefish.fragment in external/u-boot).
  partitions.push_back(ImagePartition{
      .label = "uboot_env",
      .image_file_path = AbsolutePath(instance.ap_uboot_env_image_path()),
  });
  partitions.push_back(ap_persistent_vbmeta.Partition());

  return partitions;
}

bool IsVmManagerQemu(const CuttlefishConfig& config) {
  return config.vm_manager() == VmmMode::kQemu;
}

}  // namespace

Result<InstanceCompositeDisk> InstanceCompositeDisk::Create(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSetup<FactoryResetProtectedImage::Create>::Type& frp,
    AutoSetup<BootConfigPartition::CreateIfNeeded>::Type& bootconfig_partition,
    AutoSetup<PersistentVbmeta::Create>::Type& persistent_vbmeta) {
  const auto ipath = [&instance](const std::string& path) -> std::string {
    return instance.PerInstancePath(path);
  };
  auto persistent_disk_builder =
      DiskBuilder()
          .ReadOnly(false)
          .Partitions(PersistentCompositeDiskConfig(
              instance, *bootconfig_partition, *frp, *persistent_vbmeta))
          .VmManager(config.vm_manager())
          .CrosvmPath(instance.crosvm_binary())
          .ConfigPath(ipath("persistent_composite_disk_config.txt"))
          .HeaderPath(ipath("persistent_composite_gpt_header.img"))
          .FooterPath(ipath("persistent_composite_gpt_footer.img"))
          .CompositeDiskPath(instance.persistent_composite_disk_path())
          .ResumeIfPossible(FLAGS_resume);
  CF_EXPECT(persistent_disk_builder.BuildCompositeDiskIfNecessary());

  std::string overlay_path =
      instance.PerInstancePath("persistent_composite_overlay.img");
  persistent_disk_builder.OverlayPath(overlay_path);
  if (IsVmManagerQemu(config)) {
    CF_EXPECT(persistent_disk_builder.BuildOverlayIfNecessary());
  }
  return InstanceCompositeDisk();
}

Result<std::optional<ApCompositeDisk>> ApCompositeDisk::Create(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSetup<ApPersistentVbmeta::Create>::Type& ap_persistent_vbmeta) {
  const auto ipath = [&instance](const std::string& path) -> std::string {
    return instance.PerInstancePath(path);
  };
  if (instance.ap_boot_flow() != APBootFlow::Grub) {
    return std::nullopt;
  }
  auto persistent_ap_disk_builder =
      DiskBuilder()
          .ReadOnly(false)
          .Partitions(PersistentAPCompositeDiskConfig(
              instance, CF_EXPECT(*ap_persistent_vbmeta)))
          .VmManager(config.vm_manager())
          .CrosvmPath(instance.crosvm_binary())
          .ConfigPath(ipath("ap_persistent_composite_disk_config.txt"))
          .HeaderPath(ipath("ap_persistent_composite_gpt_header.img"))
          .FooterPath(ipath("ap_persistent_composite_gpt_footer.img"))
          .CompositeDiskPath(instance.persistent_ap_composite_disk_path())
          .ResumeIfPossible(FLAGS_resume);
  CF_EXPECT(persistent_ap_disk_builder.BuildCompositeDiskIfNecessary());
  persistent_ap_disk_builder.OverlayPath(
      instance.PerInstancePath("ap_persistent_composite_overlay.img"));
  if (IsVmManagerQemu(config)) {
    CF_EXPECT(persistent_ap_disk_builder.BuildOverlayIfNecessary());
  }
  return ApCompositeDisk();
}

}  // namespace cuttlefish
