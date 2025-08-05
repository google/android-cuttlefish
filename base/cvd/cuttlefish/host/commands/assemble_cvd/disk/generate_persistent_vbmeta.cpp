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

#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_vbmeta.h"

#include <optional>
#include <string>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_config.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"
#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {
namespace {

Result<void> PrepareVBMetaImage(const std::string& path, bool has_boot_config) {
  std::vector<ChainPartition> chained_partitions = {ChainPartition{
      .name = "uboot_env",
      .rollback_index = "1",
      .key_path = TestPubKeyRsa4096(),
  }};
  if (has_boot_config) {
    chained_partitions.emplace_back(ChainPartition{
        .name = "bootconfig",
        .rollback_index = "2",
        .key_path = TestPubKeyRsa4096(),
    });
  }
  CF_EXPECT(Avb().MakeVbMetaImage(path, chained_partitions, {}, {}));
  return {};
}

}  // namespace

Result<PersistentVbmeta> PersistentVbmeta::Create(
    const std::optional<BootConfigPartition>& /* dependency */,
    const BootloaderEnvPartition& /* dependency */,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string path =
      AbsolutePath(instance.PerInstancePath("persistent_vbmeta.img"));

  CF_EXPECT(PrepareVBMetaImage(path, instance.bootconfig_supported()));

  return PersistentVbmeta(std::move(path));
}

PersistentVbmeta::PersistentVbmeta(std::string path) : path_(std::move(path)) {}

ImagePartition PersistentVbmeta::Partition() const {
  return ImagePartition{
      .label = "vbmeta",
      .image_file_path = path_,
  };
}

Result<std::optional<ApPersistentVbmeta>> ApPersistentVbmeta::Create(
    const ApBootloaderEnvPartition& /* dependency */,
    const std::optional<BootConfigPartition>& /* dependency */,
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (instance.ap_boot_flow() == APBootFlow::Grub) {
    std::string path = AbsolutePath(instance.PerInstancePath("ap_vbmeta.img"));

    CF_EXPECT(PrepareVBMetaImage(path, false));

    return ApPersistentVbmeta(std::move(path));
  } else {
    return std::nullopt;
  }
}

ApPersistentVbmeta::ApPersistentVbmeta(std::string path)
    : path_(std::move(path)) {}

ImagePartition ApPersistentVbmeta::Partition() const {
  return ImagePartition{
      .label = "vbmeta",
      .image_file_path = path_,
  };
}

}  // namespace cuttlefish
