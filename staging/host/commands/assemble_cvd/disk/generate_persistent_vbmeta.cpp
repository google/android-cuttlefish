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
#include <unordered_set>

#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"

namespace cuttlefish {

using APBootFlow = CuttlefishConfig::InstanceSpecific::APBootFlow;

class GeneratePersistentVbmetaImpl : public GeneratePersistentVbmeta {
 public:
  INJECT(GeneratePersistentVbmetaImpl(
      const CuttlefishConfig::InstanceSpecific& instance,
      InitBootloaderEnvPartition& bootloader_env,
      GeneratePersistentBootconfig& bootconfig))
      : instance_(instance),
        bootloader_env_(bootloader_env),
        bootconfig_(bootconfig) {}

  // SetupFeature
  std::string Name() const override { return "GeneratePersistentVbmeta"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {
        static_cast<SetupFeature*>(&bootloader_env_),
        static_cast<SetupFeature*>(&bootconfig_),
    };
  }

  bool Setup() override {
    if (!instance_.protected_vm()) {
      if (!PrepareVBMetaImage(instance_.vbmeta_path(),
                              instance_.bootconfig_supported())) {
        return false;
      }
    }

    if (instance_.ap_boot_flow() == APBootFlow::Grub) {
      if (!PrepareVBMetaImage(instance_.ap_vbmeta_path(), false)) {
        return false;
      }
    }

    return true;
  }

  bool PrepareVBMetaImage(const std::string& path, bool has_boot_config) {
    auto avbtool_path = HostBinaryPath("avbtool");
    Command vbmeta_cmd(avbtool_path);
    vbmeta_cmd.AddParameter("make_vbmeta_image");
    vbmeta_cmd.AddParameter("--output");
    vbmeta_cmd.AddParameter(path);
    vbmeta_cmd.AddParameter("--algorithm");
    vbmeta_cmd.AddParameter("SHA256_RSA4096");
    vbmeta_cmd.AddParameter("--key");
    vbmeta_cmd.AddParameter(
        DefaultHostArtifactsPath("etc/cvd_avb_testkey.pem"));

    vbmeta_cmd.AddParameter("--chain_partition");
    vbmeta_cmd.AddParameter("uboot_env:1:" +
                            DefaultHostArtifactsPath("etc/cvd.avbpubkey"));

    if (has_boot_config) {
      vbmeta_cmd.AddParameter("--chain_partition");
      vbmeta_cmd.AddParameter("bootconfig:2:" +
                              DefaultHostArtifactsPath("etc/cvd.avbpubkey"));
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

  const CuttlefishConfig::InstanceSpecific& instance_;
  InitBootloaderEnvPartition& bootloader_env_;
  GeneratePersistentBootconfig& bootconfig_;
};

fruit::Component<
    fruit::Required<const CuttlefishConfig::InstanceSpecific,
                    InitBootloaderEnvPartition, GeneratePersistentBootconfig>,
    GeneratePersistentVbmeta>
GeneratePersistentVbmetaComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, GeneratePersistentVbmetaImpl>()
      .bind<GeneratePersistentVbmeta, GeneratePersistentVbmetaImpl>();
}

}  // namespace cuttlefish
