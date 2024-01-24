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
#include "common/libs/utils/result.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/bootconfig_args.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/kernel_args.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

using cuttlefish::vm_manager::CrosvmManager;

DECLARE_string(vm_manager);

// Taken from external/avb/avbtool.py; this define is not in the headers
#define MAX_AVB_METADATA_SIZE 69632ul

namespace cuttlefish {
namespace {

// The ordering of tap devices we're passing to crosvm / qemu is important
// Ethernet tap device is the second one (eth1) we're passing ATM
static constexpr char kUbootPrimaryEth[] = "eth1";

void WritePausedEntrypoint(const std::string& entrypoint,
                           const CuttlefishConfig::InstanceSpecific& instance,
                           std::ostream& env) {
  if (instance.pause_in_bootloader()) {
    env << "if test $paused -ne 1; then paused=1; else " << entrypoint << "; fi";
  } else {
    env << entrypoint;
  }

  env << '\0';
}

void WriteAndroidEnvironment(
    std::ostream& env,
    const CuttlefishConfig::InstanceSpecific& instance) {
  WritePausedEntrypoint("run bootcmd_android", instance, env);

  if (!instance.boot_slot().empty()) {
    env << "android_slot_suffix=_" << instance.boot_slot() << '\0';
  }
  env << '\0';
}

void WriteEFIEnvironment(const CuttlefishConfig::InstanceSpecific& instance,
                         std::optional<std::uint16_t> partition_num,
                         std::ostream& env) {
  std::string partition_str =
      partition_num ? fmt::format("setenv devplist {:x};", *partition_num) : "";
  WritePausedEntrypoint(
      partition_str +
          "load virtio 0:${devplist} ${loadaddr} efi/boot/bootaa64.efi "
          "&& bootefi ${loadaddr} ${fdtcontroladdr}; "
          "load virtio 0:${devplist} ${loadaddr} efi/boot/bootia32.efi && "
          "bootefi ${loadaddr} ${fdtcontroladdr};"
          "load virtio 0:${devplist} ${loadaddr} efi/boot/bootriscv64.efi && "
          "bootefi ${loadaddr} ${fdtcontroladdr}",
      instance, env);
}

size_t WriteEnvironment(const CuttlefishConfig::InstanceSpecific& instance,
                        const CuttlefishConfig::InstanceSpecific::BootFlow& flow,
                        const std::string& kernel_args,
                        const std::string& env_path) {
  std::ostringstream env;

  env << "ethprime=" << kUbootPrimaryEth << '\0';
  if (!kernel_args.empty()) {
    env << "uenvcmd=setenv bootargs \"$cbootargs " << kernel_args << "\" && ";
  } else {
    env << "uenvcmd=setenv bootargs \"$cbootargs\" && ";
  }

  switch (flow) {
    case CuttlefishConfig::InstanceSpecific::BootFlow::Android:
      WriteAndroidEnvironment(env, instance);
      break;
    case CuttlefishConfig::InstanceSpecific::BootFlow::AndroidEfiLoader:
      WriteEFIEnvironment(instance, 1, env);
      break;
    case CuttlefishConfig::InstanceSpecific::BootFlow::ChromeOs:
      WriteEFIEnvironment(instance, 2, env);
      break;
    case CuttlefishConfig::InstanceSpecific::BootFlow::ChromeOsDisk:
      WriteEFIEnvironment(instance, 12, env);
      break;
    case CuttlefishConfig::InstanceSpecific::BootFlow::Fuchsia:
    case CuttlefishConfig::InstanceSpecific::BootFlow::Linux:
      WriteEFIEnvironment(instance, {}, env);
      break;
  }

  std::string env_str = env.str();
  std::ofstream file_out(env_path.c_str(), std::ios::binary);
  file_out << env_str;

  if (!file_out.good()) {
    return 0;
  }

  return env_str.length();
}

}  // namespace

class InitBootloaderEnvPartitionImpl : public InitBootloaderEnvPartition {
 public:
  INJECT(InitBootloaderEnvPartitionImpl(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitBootloaderEnvPartitionImpl"; }
  bool Enabled() const override { return !instance_.protected_vm(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (instance_.ap_boot_flow() == CuttlefishConfig::InstanceSpecific::APBootFlow::Grub) {
      CF_EXPECT(PrepareBootEnvImage(
          instance_.ap_uboot_env_image_path(),
          CuttlefishConfig::InstanceSpecific::BootFlow::Linux));
    }
    CF_EXPECT(PrepareBootEnvImage(instance_.uboot_env_image_path(),
                                  instance_.boot_flow()));
    return {};
  }

  std::unordered_map<std::string, std::string> ReplaceKernelBootArgs(
      const std::unordered_map<std::string, std::string>& args) {
    std::unordered_map<std::string, std::string> ret;
    std::transform(std::begin(args), std::end(args),
                   std::inserter(ret, ret.end()), [](const auto& kv) {
                     const auto& k = kv.first;
                     const auto& v = kv.second;
                     return std::make_pair(
                         android::base::StringReplace(k, " kernel.", " ", true),
                         v);
                   });
    return ret;
  }

  Result<void> PrepareBootEnvImage(
      const std::string& image_path,
      const CuttlefishConfig::InstanceSpecific::BootFlow& flow) {
    auto tmp_boot_env_image_path = image_path + ".tmp";
    auto uboot_env_path = instance_.PerInstancePath("mkenvimg_input");
    auto kernel_cmdline = android::base::Join(
        KernelCommandLineFromConfig(config_, instance_), " ");
    // If the bootconfig isn't supported in the guest kernel, the bootconfig
    // args need to be passed in via the uboot env. This won't be an issue for
    // protect kvm which is running a kernel with bootconfig support.
    if (!instance_.bootconfig_supported()) {
      auto bootconfig_args =
          CF_EXPECT(BootconfigArgsFromConfig(config_, instance_));

      // "androidboot.hardware" kernel parameter has changed to "hardware" in
      // bootconfig and needs to be replaced before being used in the kernel
      // cmdline.
      auto bootconfig_hardware_it = bootconfig_args.find("hardware");
      if (bootconfig_hardware_it != bootconfig_args.end()) {
        bootconfig_args["androidboot.hardware"] =
            bootconfig_hardware_it->second;
        bootconfig_args.erase(bootconfig_hardware_it);
      }

      // TODO(b/182417593): Until we pass the module parameters through
      // modules.options, we pass them through bootconfig using
      // 'kernel.<key>=<value>' But if we don't support bootconfig, we need to
      // rename them back to the old cmdline version
      bootconfig_args = ReplaceKernelBootArgs(bootconfig_args);

      kernel_cmdline +=
          " " + CF_EXPECT(BootconfigArgsString(bootconfig_args, " "));
    }

    CF_EXPECTF(
        WriteEnvironment(instance_, flow, kernel_cmdline, uboot_env_path),
        "Unable to write out plaintext env '{}'", uboot_env_path);

    auto mkimage_path = HostBinaryPath("mkenvimage_slim");
    Command cmd(mkimage_path);
    cmd.AddParameter("-output_path");
    cmd.AddParameter(tmp_boot_env_image_path);
    cmd.AddParameter("-input_path");
    cmd.AddParameter(uboot_env_path);
    int success = cmd.Start().Wait();
    CF_EXPECTF(success == 0,
               "Unable to run mkenvimage_slim. Exited with status {}", success);

    const off_t boot_env_size_bytes = AlignToPowerOf2(
        MAX_AVB_METADATA_SIZE + 4096, PARTITION_SIZE_SHIFT);

    auto avbtool_path = HostBinaryPath("avbtool");
    Command boot_env_hash_footer_cmd(avbtool_path);
    boot_env_hash_footer_cmd.AddParameter("add_hash_footer");
    boot_env_hash_footer_cmd.AddParameter("--image");
    boot_env_hash_footer_cmd.AddParameter(tmp_boot_env_image_path);
    boot_env_hash_footer_cmd.AddParameter("--partition_size");
    boot_env_hash_footer_cmd.AddParameter(boot_env_size_bytes);
    boot_env_hash_footer_cmd.AddParameter("--partition_name");
    boot_env_hash_footer_cmd.AddParameter("uboot_env");
    boot_env_hash_footer_cmd.AddParameter("--key");
    boot_env_hash_footer_cmd.AddParameter(
        DefaultHostArtifactsPath("etc/cvd_avb_testkey.pem"));
    boot_env_hash_footer_cmd.AddParameter("--algorithm");
    boot_env_hash_footer_cmd.AddParameter("SHA256_RSA4096");
    success = boot_env_hash_footer_cmd.Start().Wait();
    CF_EXPECTF(success == 0,
               "Unable to append hash footer. Exited with status {}", success);

    if (!FileExists(image_path) ||
        ReadFile(image_path) != ReadFile(tmp_boot_env_image_path)) {
      CF_EXPECT(RenameFile(tmp_boot_env_image_path, image_path),
                "Unable to delete the old env image");
      LOG(DEBUG) << "Updated bootloader environment image.";
    } else {
      RemoveFile(tmp_boot_env_image_path);
    }

    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 InitBootloaderEnvPartition>
InitBootloaderEnvPartitionComponent() {
  return fruit::createComponent()
      .bind<InitBootloaderEnvPartition, InitBootloaderEnvPartitionImpl>()
      .addMultibinding<SetupFeature, InitBootloaderEnvPartition>();
}

} // namespace cuttlefish
