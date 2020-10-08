/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "host/libs/config/kernel_args.h"

#include <array>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

using vm_manager::CrosvmManager;
using vm_manager::QemuManager;

namespace {

template<typename T>
void AppendVector(std::vector<T>* destination, const std::vector<T>& source) {
  destination->insert(destination->end(), source.begin(), source.end());
}

template<typename S, typename T>
std::string concat(const S& s, const T& t) {
  std::ostringstream os;
  os << s << t;
  return os.str();
}

std::string mac_to_str(const std::array<unsigned char, 6>& mac) {
  std::ostringstream stream;
  stream << std::hex << (int) mac[0];
  for (int i = 1; i < 6; i++) {
    stream << ":" << std::hex << (int) mac[i];
  }
  return stream.str();
}

// TODO(schuffelen): Move more of this into host/libs/vm_manager, as a
// substitute for the vm_manager comparisons.
std::vector<std::string> VmManagerKernelCmdline(const CuttlefishConfig& config) {
  std::vector<std::string> vm_manager_cmdline;
  if (config.vm_manager() == QemuManager::name() || config.use_bootloader()) {
    // crosvm sets up the console= earlycon= panic= flags for us if booting straight to
    // the kernel, but QEMU and the bootloader via crosvm does not.
    AppendVector(&vm_manager_cmdline, {"console=hvc0", "panic=-1"});
    if (HostArch() == "aarch64") {
      if (config.vm_manager() == QemuManager::name()) {
        // To update the pl011 address:
        // $ qemu-system-aarch64 -machine virt -cpu cortex-a57 -machine dumpdtb=virt.dtb
        // $ dtc -O dts -o virt.dts -I dtb virt.dtb
        // In the virt.dts file, look for a uart node
        vm_manager_cmdline.push_back(" earlycon=pl011,mmio32,0x9000000");
      } else {
        // Crosvm ARM only supports earlycon uart over mmio.
        vm_manager_cmdline.push_back(" earlycon=uart8250,mmio,0x3f8");
      }
    } else {
      // To update the uart8250 address:
      // $ qemu-system-x86_64 -kernel bzImage -serial stdio | grep ttyS0
      // Only 'io' mode works; mmio and mmio32 do not
      vm_manager_cmdline.push_back("earlycon=uart8250,io,0x3f8");

      if (config.vm_manager() == QemuManager::name()) {
        // crosvm doesn't support ACPI PNP, but QEMU does. We need to disable
        // it on QEMU so that the ISA serial ports aren't claimed by ACPI, so
        // we can use serdev with platform devices instead
        vm_manager_cmdline.push_back("pnpacpi=off");

        // crosvm sets up the ramoops.xx= flags for us, but QEMU does not.
        // See external/crosvm/x86_64/src/lib.rs
        // this feature is not supported on aarch64
        vm_manager_cmdline.push_back("ramoops.mem_address=0x100000000");
        vm_manager_cmdline.push_back("ramoops.mem_size=0x200000");
        vm_manager_cmdline.push_back("ramoops.console_size=0x80000");
        vm_manager_cmdline.push_back("ramoops.record_size=0x80000");
        vm_manager_cmdline.push_back("ramoops.dump_oops=1");
      } else {
        // crosvm requires these additional parameters on x86_64 in bootloader mode
        AppendVector(&vm_manager_cmdline, {"pci=noacpi", "reboot=k"});
      }
    }
  }

  if (config.console()) {
    std::string console_dev;
    auto can_use_virtio_console = !config.kgdb() && !config.use_bootloader();
    if (can_use_virtio_console) {
      // If kgdb and the bootloader are disabled, the Android serial console spawns on a
      // virtio-console port. If the bootloader is enabled, virtio console can't be used
      // since uboot doesn't support it.
      console_dev = "hvc1";
    } else {
      // crosvm ARM does not support ttyAMA. ttyAMA is a part of ARM arch.
      if (HostArch() == "aarch64" && config.vm_manager() != CrosvmManager::name()) {
        console_dev = "ttyAMA0";
      } else {
        console_dev = "ttyS0";
      }
    }

    vm_manager_cmdline.push_back("androidboot.console=" + console_dev);
    if (config.kgdb()) {
      AppendVector(
          &vm_manager_cmdline,
          {"kgdboc_earlycon", "kgdbcon", "kgdboc=" + console_dev});
    }
  } else {
    // Specify an invalid path under /dev, so the init process will disable the
    // console service due to the console not being found. On physical devices,
    // it is enough to not specify androidboot.console= *and* not specify the
    // console= kernel command line parameter, because the console and kernel
    // dmesg are muxed. However, on cuttlefish, we don't need to mux, and would
    // prefer to retain the kernel dmesg logging, so we must work around init
    // falling back to the check for /dev/console (which we'll always have).
    vm_manager_cmdline.push_back("androidboot.console=invalid");
  }
  return vm_manager_cmdline;
}

} // namespace

std::vector<std::string> KernelCommandLineFromConfig(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<std::string> kernel_cmdline;

  AppendVector(&kernel_cmdline, VmManagerKernelCmdline(config));
  AppendVector(&kernel_cmdline, config.boot_image_kernel_cmdline());
  auto vmm = vm_manager::GetVmManager(config.vm_manager());
  AppendVector(&kernel_cmdline, vmm->ConfigureGpuMode(config.gpu_mode()));
  AppendVector(&kernel_cmdline, vmm->ConfigureBootDevices());

  if (config.enable_gnss_grpc_proxy()) {
    kernel_cmdline.push_back("gnss_cmdline.serdev=serial8250/serial0/serial0-0");
    kernel_cmdline.push_back("gnss_cmdline.type=0");
    kernel_cmdline.push_back("serdev_ttyport.pdev_tty_port=ttyS1");
  }

  kernel_cmdline.push_back(concat("androidboot.serialno=", instance.serial_number()));
  kernel_cmdline.push_back(concat("androidboot.lcd_density=", config.dpi()));
  kernel_cmdline.push_back(concat(
      "androidboot.setupwizard_mode=", config.setupwizard_mode()));
  if (!config.use_bootloader()) {
    std::string slot_suffix;
    if (config.boot_slot().empty()) {
      slot_suffix = "_a";
    } else {
      slot_suffix = "_" + config.boot_slot();
    }
    kernel_cmdline.push_back(concat("androidboot.slot_suffix=", slot_suffix));
  }
  if (!config.guest_enforce_security()) {
    kernel_cmdline.push_back("androidboot.selinux=permissive");
  }
  if (config.guest_audit_security()) {
    kernel_cmdline.push_back("audit=1");
  } else {
    kernel_cmdline.push_back("audit=0");
  }
  if (config.guest_force_normal_boot()) {
    kernel_cmdline.push_back("androidboot.force_normal_boot=1");
  }

  if (instance.tombstone_receiver_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_tombstone_port=", instance.tombstone_receiver_port()));
  }

  if (instance.config_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.cuttlefish_config_server_port=", instance.config_server_port()));
  }

  if (instance.keyboard_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_keyboard_port=", instance.keyboard_server_port()));
  }

  if (instance.touch_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_touch_port=", instance.touch_server_port()));
  }

  if (config.enable_vehicle_hal_grpc_server() && instance.vehicle_hal_server_port() &&
      FileExists(config.vehicle_hal_grpc_server_binary())) {
    constexpr int vehicle_hal_server_cid = 2;
    kernel_cmdline.push_back(concat("androidboot.vendor.vehiclehal.server.cid=", vehicle_hal_server_cid));
    kernel_cmdline.push_back(concat("androidboot.vendor.vehiclehal.server.port=", instance.vehicle_hal_server_port()));
  }

  if (instance.audiocontrol_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vendor.audiocontrol.server.cid=", instance.vsock_guest_cid()));
    kernel_cmdline.push_back(concat("androidboot.vendor.audiocontrol.server.port=", instance.audiocontrol_server_port()));
  }

  if (instance.frames_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_frames_port=", instance.frames_server_port()));
  }

  kernel_cmdline.push_back(concat("androidboot.vsock_keymaster_port=",
                                  instance.keymaster_vsock_port()));

  kernel_cmdline.push_back(concat("androidboot.vsock_gatekeeper_port=",
                                  instance.gatekeeper_vsock_port()));

  if (config.enable_modem_simulator() &&
      instance.modem_simulator_ports() != "") {
    kernel_cmdline.push_back(concat("androidboot.modem_simulator_ports=",
                                    instance.modem_simulator_ports()));
  }

  // TODO(b/158131610): Set this in crosvm instead
  kernel_cmdline.push_back(concat("androidboot.wifi_mac_address=",
                                  mac_to_str(instance.wifi_mac_address())));

  kernel_cmdline.push_back("androidboot.verifiedbootstate=orange");

  AppendVector(&kernel_cmdline, config.extra_kernel_cmdline());

  return kernel_cmdline;
}

} // namespace cuttlefish
