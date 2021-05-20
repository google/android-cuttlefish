/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/commands/assemble_cvd/boot_config.h"

#include <fstream>
#include <sstream>
#include <string>

#include <sys/stat.h>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/bootconfig_args.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/kernel_args.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

using cuttlefish::vm_manager::CrosvmManager;

DECLARE_bool(pause_in_bootloader);
DECLARE_string(vm_manager);

namespace cuttlefish {
namespace {

size_t WriteEnvironment(const CuttlefishConfig& config,
                        const std::string& kernel_args,
                        const std::string& env_path) {
  std::ostringstream env;
  env << "bootargs=" << kernel_args << '\0';
  if (!config.boot_slot().empty()) {
      env << "android_slot_suffix=_" << config.boot_slot() << '\0';
  }

  if(FLAGS_pause_in_bootloader) {
    env << "bootdelay=-1" << '\0';
  } else {
    env << "bootdelay=0" << '\0';
  }

  // Note that the 0 index points to the GPT table.
  env << "bootcmd=boot_android virtio 0#misc" << '\0';
  if (FLAGS_vm_manager == CrosvmManager::name() &&
      config.target_arch() == Arch::Arm64) {
    env << "fdtaddr=0x80000000" << '\0';
  } else {
    env << "fdtaddr=0x40000000" << '\0';
  }
  env << '\0';
  std::string env_str = env.str();
  std::ofstream file_out(env_path.c_str(), std::ios::binary);
  file_out << env_str;

  if(!file_out.good()) {
    return 0;
  }

  return env_str.length();
}

}  // namespace


bool InitBootloaderEnvPartition(const CuttlefishConfig& config,
                                const CuttlefishConfig::InstanceSpecific& instance) {
  auto boot_env_image_path = instance.uboot_env_image_path();
  auto tmp_boot_env_image_path = boot_env_image_path + ".tmp";
  auto uboot_env_path = instance.PerInstancePath("mkenvimg_input");
  auto kernel_cmdline =
      android::base::Join(KernelCommandLineFromConfig(config), " ");
  // If the bootconfig isn't supported in the guest kernel, the bootconfig args
  // need to be passed in via the uboot env. This won't be an issue for protect
  // kvm which is running a kernel with bootconfig support.
  if (!config.bootconfig_supported()) {
    auto bootconfig_args =
        android::base::Join(BootconfigArgsFromConfig(config, instance), " ");
    // "androidboot.hardware" kernel parameter has changed to "hardware" in
    // bootconfig and needs to be replaced before being used in the kernel
    // cmdline.
    bootconfig_args = android::base::StringReplace(
        bootconfig_args, " hardware=", " androidboot.hardware=", true);
    // TODO(b/182417593): Until we pass the module parameters through
    // modules.options, we pass them through bootconfig using
    // 'kernel.<key>=<value>' But if we don't support bootconfig, we need to
    // rename them back to the old cmdline version
    bootconfig_args =
        android::base::StringReplace(bootconfig_args, " kernel.", " ", true);
    kernel_cmdline += " ";
    kernel_cmdline += bootconfig_args;
  }
  if (!WriteEnvironment(config, kernel_cmdline, uboot_env_path)) {
    LOG(ERROR) << "Unable to write out plaintext env '" << uboot_env_path << ".'";
    return false;
  }

  auto mkimage_path = HostBinaryPath("mkenvimage");
  Command cmd(mkimage_path);
  cmd.AddParameter("-s");
  cmd.AddParameter("4096");
  cmd.AddParameter("-o");
  cmd.AddParameter(tmp_boot_env_image_path);
  cmd.AddParameter(uboot_env_path);
  int success = cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run mkenvimage. Exited with status " << success;
    return false;
  }

  if(!FileExists(boot_env_image_path) || ReadFile(boot_env_image_path) != ReadFile(tmp_boot_env_image_path)) {
    if(!RenameFile(tmp_boot_env_image_path, boot_env_image_path)) {
      LOG(ERROR) << "Unable to delete the old env image.";
      return false;
    }
    LOG(DEBUG) << "Updated bootloader environment image.";
  } else {
    RemoveFile(tmp_boot_env_image_path);
  }

  return true;
}

} // namespace cuttlefish
