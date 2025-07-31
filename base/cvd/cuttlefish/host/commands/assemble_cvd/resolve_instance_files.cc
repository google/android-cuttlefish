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

#include "cuttlefish/host/commands/assemble_cvd/resolve_instance_files.h"

#include <sys/statvfs.h>

#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parsebool.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/initramfs_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/instance_nums.h"

namespace cuttlefish {

Result<void> ResolveInstanceFiles(const BootImageFlag& boot_image,
                                  const InitramfsPathFlag& initramfs_path,
                                  const KernelPathFlag& kernel_path,
                                  const SystemImageDirFlag& system_image_dir) {
  // It is conflict (invalid) to pass both kernel_path/initramfs_path
  // and image file paths.
  bool flags_kernel_initramfs_has_input =
      (!kernel_path.HasValue()) || (!initramfs_path.HasValue());
  bool flags_image_has_input =
      (!FLAGS_super_image.empty()) || (!FLAGS_vendor_boot_image.empty()) ||
      (!FLAGS_vbmeta_vendor_dlkm_image.empty()) ||
      (!FLAGS_vbmeta_system_dlkm_image.empty()) || (!boot_image.IsDefault());
  CF_EXPECT(!(flags_kernel_initramfs_has_input && flags_image_has_input),
            "Cannot pass both kernel_path/initramfs_path and image file paths");

  std::string default_super_image = "";
  std::string default_vendor_boot_image = "";
  std::string default_vbmeta_image = "";
  std::string default_vbmeta_system_image = "";
  std::string default_vbmeta_vendor_dlkm_image = "";
  std::string default_vbmeta_system_dlkm_image = "";
  std::string default_16k_kernel_image = "";
  std::string default_16k_ramdisk_image = "";
  std::string vvmtruststore_path = "";

  std::string comma_str = "";
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  auto default_vvmtruststore_file_name =
      android::base::Split(FLAGS_default_vvmtruststore_file_name, ",");
  for (int instance_index = 0; instance_index < instance_nums.size();
       instance_index++) {
    if (instance_index > 0) {
      comma_str = ",";
    }
    std::string cur_system_image_dir =
        system_image_dir.ForIndex(instance_index);

    // If user did not specify location of either of these files, expect them to
    // be placed in --system_image_dir location.
    default_super_image += comma_str + cur_system_image_dir + "/super.img";
    default_vendor_boot_image +=
        comma_str + cur_system_image_dir + "/vendor_boot.img";
    default_vbmeta_image += comma_str + cur_system_image_dir + "/vbmeta.img";
    default_vbmeta_system_image +=
        comma_str + cur_system_image_dir + "/vbmeta_system.img";
    default_vbmeta_vendor_dlkm_image +=
        comma_str + cur_system_image_dir + "/vbmeta_vendor_dlkm.img";
    default_vbmeta_system_dlkm_image +=
        comma_str + cur_system_image_dir + "/vbmeta_system_dlkm.img";

    if (instance_index < default_vvmtruststore_file_name.size()) {
      if (default_vvmtruststore_file_name[instance_index].empty()) {
        vvmtruststore_path += comma_str;
      } else {
        vvmtruststore_path += comma_str + cur_system_image_dir + "/" +
                              default_vvmtruststore_file_name[instance_index];
      }
    }
  }
  SetCommandLineOptionWithMode("super_image", default_super_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vendor_boot_image",
                               default_vendor_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_image", default_vbmeta_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_system_image",
                               default_vbmeta_system_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_vendor_dlkm_image",
                               default_vbmeta_vendor_dlkm_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_system_dlkm_image",
                               default_vbmeta_system_dlkm_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vvmtruststore_path", vvmtruststore_path.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  return {};
}

}  // namespace cuttlefish
