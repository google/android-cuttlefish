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

#include <string>

#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_config.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

Result<void> PrepareVBMetaImage(const std::string& path, bool has_boot_config) {
  std::unique_ptr<Avb> avbtool = GetDefaultAvb();
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
  CF_EXPECT(avbtool->MakeVbMetaImage(path, chained_partitions, {}, {}));
  return {};
}

}  // namespace

Result<void> GeneratePersistentVbmeta(
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSetup<InitBootloaderEnvPartition>::Type& /* dependency */,
    AutoSetup<BootConfigPartition::CreateIfNeeded>::Type& /* dependency */) {
  CF_EXPECT(PrepareVBMetaImage(instance.vbmeta_path(),
                               instance.bootconfig_supported()));
  if (instance.ap_boot_flow() == APBootFlow::Grub) {
    CF_EXPECT(PrepareVBMetaImage(instance.ap_vbmeta_path(), false));
  }
  return {};
}

}  // namespace cuttlefish
