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

#include "host/commands/assemble_cvd/disk/disk.h"

#include <string>

#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

using APBootFlow = CuttlefishConfig::InstanceSpecific::APBootFlow;

static bool PrepareVBMetaImage(const std::string& path, bool has_boot_config) {
  auto avbtool_path = HostBinaryPath("avbtool");
  Command vbmeta_cmd(avbtool_path);
  vbmeta_cmd.AddParameter("make_vbmeta_image");
  vbmeta_cmd.AddParameter("--output");
  vbmeta_cmd.AddParameter(path);
  vbmeta_cmd.AddParameter("--algorithm");
  vbmeta_cmd.AddParameter("SHA256_RSA4096");
  vbmeta_cmd.AddParameter("--key");
  vbmeta_cmd.AddParameter(TestKeyRsa4096());

  vbmeta_cmd.AddParameter("--chain_partition");
  vbmeta_cmd.AddParameter("uboot_env:1:" + TestPubKeyRsa4096());

  if (has_boot_config) {
    vbmeta_cmd.AddParameter("--chain_partition");
    vbmeta_cmd.AddParameter("bootconfig:2:" + TestPubKeyRsa4096());
  }

  bool success = vbmeta_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to create persistent vbmeta. Exited with status "
               << success;
    return false;
  }

  const auto vbmeta_size = FileSize(path);
  if (vbmeta_size > VBMETA_MAX_SIZE) {
    LOG(ERROR) << "Generated vbmeta - " << path
               << " is larger than the expected " << VBMETA_MAX_SIZE
               << ". Stopping.";
    return false;
  }
  if (vbmeta_size != VBMETA_MAX_SIZE) {
    auto fd = SharedFD::Open(path, O_RDWR);
    if (!fd->IsOpen() || fd->Truncate(VBMETA_MAX_SIZE) != 0) {
      LOG(ERROR) << "`truncate --size=" << VBMETA_MAX_SIZE << " " << path
                 << "` failed: " << fd->StrError();
      return false;
    }
  }
  return true;
}

Result<void> GeneratePersistentVbmeta(
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSetup<InitBootloaderEnvPartition>::Type& /* dependency */,
    AutoSetup<GeneratePersistentBootconfig>::Type& /* dependency */) {
  if (!instance.protected_vm()) {
    CF_EXPECT(PrepareVBMetaImage(instance.vbmeta_path(),
                                 instance.bootconfig_supported()));
  }
  if (instance.ap_boot_flow() == APBootFlow::Grub) {
    CF_EXPECT(PrepareVBMetaImage(instance.ap_vbmeta_path(), false));
  }
  return {};
}

}  // namespace cuttlefish
