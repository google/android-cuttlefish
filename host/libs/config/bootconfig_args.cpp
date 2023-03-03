/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/libs/config/bootconfig_args.h"

#include <array>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

using vm_manager::CrosvmManager;
using vm_manager::QemuManager;

namespace {

template <typename T>
void AppendMapWithReplacement(T* destination, const T& source) {
  for (const auto& [k, v] : source) {
    (*destination)[k] = v;
  }
}

// TODO(schuffelen): Move more of this into host/libs/vm_manager, as a
// substitute for the vm_manager comparisons.
Result<std::unordered_map<std::string, std::string>> VmManagerBootconfig(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::unordered_map<std::string, std::string> bootconfig_args;
  if (instance.console()) {
    bootconfig_args["androidboot.console"] = instance.console_dev();
    bootconfig_args["androidboot.serialconsole"] = "1";
  } else {
    // Specify an invalid path under /dev, so the init process will disable the
    // console service due to the console not being found. On physical devices,
    // *and on older kernels* it is enough to not specify androidboot.console=
    // *and* not specify the console= kernel command line parameter, because
    // the console and kernel dmesg are muxed. However, on cuttlefish, we don't
    // need to mux, and would prefer to retain the kernel dmesg logging, so we
    // must work around init falling back to the check for /dev/console (which
    // we'll always have).
    //bootconfig_args["androidboot.console"] = "invalid";
    // The bug above has been fixed in Android 14 and later so we can just
    // specify androidboot.serialconsole=0 instead.
    bootconfig_args["androidboot.serialconsole"] = "0";
  }
  return bootconfig_args;
}

}  // namespace

Result<std::unordered_map<std::string, std::string>> BootconfigArgsFromConfig(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::unordered_map<std::string, std::string> bootconfig_args;

  AppendMapWithReplacement(&bootconfig_args,
                           CF_EXPECT(VmManagerBootconfig(instance)));

  auto vmm =
      vm_manager::GetVmManager(config.vm_manager(), instance.target_arch());
  AppendMapWithReplacement(&bootconfig_args,
                           CF_EXPECT(vmm->ConfigureBootDevices(
                               instance.virtual_disk_paths().size(),
                               instance.hwcomposer() != kHwComposerNone)));

  AppendMapWithReplacement(&bootconfig_args,
                           CF_EXPECT(vmm->ConfigureGraphics(instance)));

  bootconfig_args["androidboot.serialno"] = instance.serial_number();
  bootconfig_args["androidboot.ddr_size"] =
      std::to_string(instance.ddr_mem_mb()) + "MB";

  // TODO(b/131884992): update to specify multiple once supported.
  const auto display_configs = instance.display_configs();
  if (!display_configs.empty()) {
    bootconfig_args["androidboot.lcd_density"] =
        std::to_string(display_configs[0].dpi);
  }

  bootconfig_args["androidboot.setupwizard_mode"] = instance.setupwizard_mode();

  bootconfig_args["androidboot.enable_bootanimation"] =
      std::to_string(instance.enable_bootanimation());

  if (!instance.guest_enforce_security()) {
    bootconfig_args["androidboot.selinux"] = "permissive";
  }

  if (instance.tombstone_receiver_port()) {
    bootconfig_args["androidboot.vsock_tombstone_port"] =
        std::to_string(instance.tombstone_receiver_port());
  }

  const auto enable_confui =
      (config.vm_manager() == QemuManager::name() ? 0 : 1);
  bootconfig_args["androidboot.enable_confirmationui"] =
      std::to_string(enable_confui);

  if (instance.config_server_port()) {
    bootconfig_args["androidboot.cuttlefish_config_server_port"] =
        std::to_string(instance.config_server_port());
  }

  if (instance.keyboard_server_port()) {
    bootconfig_args["androidboot.vsock_keyboard_port"] =
        std::to_string(instance.keyboard_server_port());
  }

  if (instance.touch_server_port()) {
    bootconfig_args["androidboot.vsock_touch_port"] =
        std::to_string(instance.touch_server_port());
  }

  if (instance.enable_vehicle_hal_grpc_server() &&
      instance.vehicle_hal_server_port() &&
      FileExists(VehicleHalGrpcServerBinary())) {
    constexpr int vehicle_hal_server_cid = 2;
    bootconfig_args["androidboot.vendor.vehiclehal.server.cid"] =
        std::to_string(vehicle_hal_server_cid);
    bootconfig_args["androidboot.vendor.vehiclehal.server.port"] =
        std::to_string(instance.vehicle_hal_server_port());
  }

  if (instance.audiocontrol_server_port()) {
    bootconfig_args["androidboot.vendor.audiocontrol.server.cid"] =
        std::to_string(instance.vsock_guest_cid());
    bootconfig_args["androidboot.vendor.audiocontrol.server.port"] =
        std::to_string(instance.audiocontrol_server_port());
  }

  if (!instance.enable_audio()) {
    bootconfig_args["androidboot.audio.tinyalsa.ignore_output"] = "true";
    bootconfig_args["androidboot.audio.tinyalsa.simulate_input"] = "true";
  }

  if (instance.camera_server_port()) {
    bootconfig_args["androidboot.vsock_camera_port"] =
        std::to_string(instance.camera_server_port());
    bootconfig_args["androidboot.vsock_camera_cid"] =
        std::to_string(instance.vsock_guest_cid());
  }

  if (instance.enable_modem_simulator() &&
      instance.modem_simulator_ports() != "") {
    bootconfig_args["androidboot.modem_simulator_ports"] =
        instance.modem_simulator_ports();
  }

  // Once all Cuttlefish kernel versions are at least 5.15, filename encryption
  // will not need to be set conditionally. HCTR2 will always be available.
  // At that point fstab.cf.f2fs.cts and fstab.cf.ext4.cts can be removed.
  std::string fstab_suffix = fmt::format("cf.{}.{}", instance.userdata_format(),
                                         instance.filename_encryption_mode());

  bootconfig_args["androidboot.fstab_suffix"] = fstab_suffix;

  bootconfig_args["androidboot.wifi_mac_prefix"] =
      std::to_string(instance.wifi_mac_prefix());

  // Non-native architecture implies a significantly slower execution speed, so
  // set a large timeout multiplier.
  if (!IsHostCompatible(instance.target_arch())) {
    bootconfig_args["androidboot.hw_timeout_multiplier"] = "50";
  }

  // TODO(b/217564326): improve this checks for a hypervisor in the VM.
  if (instance.target_arch() == Arch::X86 ||
      instance.target_arch() == Arch::X86_64) {
    bootconfig_args["androidboot.hypervisor.version"] =
        "cf-" + config.vm_manager();
    bootconfig_args["androidboot.hypervisor.vm.supported"] = "1";
  } else {
    bootconfig_args["androidboot.hypervisor.vm.supported"] = "0";
  }
  bootconfig_args["androidboot.hypervisor.protected_vm.supported"] = "0";
  if (!instance.kernel_path().empty()) {
    bootconfig_args["androidboot.kernel_hotswapped"] = "1";
  }
  if (!instance.initramfs_path().empty()) {
    bootconfig_args["androidboot.ramdisk_hotswapped"] = "1";
  }

  for (const std::string& kv : config.extra_bootconfig_args()) {
    if (kv.empty()) {
      continue;
    }
    const auto& parts = android::base::Split(kv, "=");
    CF_EXPECT_EQ(parts.size(), 2,
                 "Failed to parse --extra_bootconfig_args: \"" << kv << "\"");
    bootconfig_args[parts[0]] = parts[1];
  }

  return bootconfig_args;
}

Result<std::string> BootconfigArgsString(
    const std::unordered_map<std::string, std::string>& args,
    const std::string& separator) {
  std::vector<std::string> combined_args;
  for (const auto& [k, v] : args) {
    CF_EXPECT(!v.empty(), "Found empty bootconfig value for " << k);
    combined_args.push_back(k + "=" + v);
  }
  return android::base::Join(combined_args, separator);
}

}  // namespace cuttlefish
