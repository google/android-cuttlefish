/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/disk/os_composite_disk.h"

#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/disk/android_composite_disk_config.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/android_efi_loader_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/chromeos_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/fuchsia_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/linux_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/boot_flow.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

std::vector<ImagePartition> GetOsCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance,
    const MetadataImage& metadata, const MiscImage& misc,
    const SystemImageDirFlag& system_image_dir) {
  switch (instance.boot_flow()) {
    case BootFlow::Android:
      return AndroidCompositeDiskConfig(instance, metadata, misc,
                                        system_image_dir);
    case BootFlow::AndroidEfiLoader:
      return AndroidEfiLoaderCompositeDiskConfig(instance, metadata, misc,
                                                 system_image_dir);
    case BootFlow::ChromeOs:
      return ChromeOsCompositeDiskConfig(instance);
    case BootFlow::ChromeOsDisk:
      return {};
    case BootFlow::Linux:
      return LinuxCompositeDiskConfig(instance);
    case BootFlow::Fuchsia:
      return FuchsiaCompositeDiskConfig(instance);
  }
}

}  // namespace cuttlefish
