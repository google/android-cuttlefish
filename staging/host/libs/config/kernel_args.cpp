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
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {

using vm_manager::QemuManager;

namespace {

template<typename T>
void AppendVector(std::vector<T>* destination, const std::vector<T>& source) {
  destination->insert(destination->end(), source.begin(), source.end());
}

// TODO(schuffelen): Move more of this into host/libs/vm_manager, as a
// substitute for the vm_manager comparisons.
std::vector<std::string> VmManagerKernelCmdline(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<std::string> vm_manager_cmdline;
  if (config.vm_manager() == QemuManager::name()) {
    Arch target_arch = instance.target_arch();
    if (target_arch == Arch::Arm64 || target_arch == Arch::Arm) {
      if (instance.enable_kernel_log()) {
        vm_manager_cmdline.push_back("console=hvc0");

        // To update the pl011 address:
        // $ qemu-system-aarch64 -machine virt -cpu cortex-a57 -machine dumpdtb=virt.dtb
        // $ dtc -O dts -o virt.dts -I dtb virt.dtb
        // In the virt.dts file, look for a uart node
        vm_manager_cmdline.push_back("earlycon=pl011,mmio32,0x9000000");
      }
    } else if (target_arch == Arch::RiscV64) {
        vm_manager_cmdline.push_back("console=hvc0");

        // To update the uart8250 address:
        // $ qemu-system-riscv64 -machine virt -machine dumpdtb=virt.dtb
        // $ dtc -O dts -o virt.dts -I dtb virt.dtb
        // In the virt.dts file, look for a uart node
        // Only 'mmio' mode works; mmio32 does not
        vm_manager_cmdline.push_back("earlycon=uart8250,mmio,0x10000000");
    } else {
      if (instance.enable_kernel_log()) {
        vm_manager_cmdline.push_back("console=hvc0");

        // To update the uart8250 address:
        // $ qemu-system-x86_64 -kernel bzImage -serial stdio | grep ttyS0
        // Only 'io' mode works; mmio and mmio32 do not
        vm_manager_cmdline.push_back("earlycon=uart8250,io,0x3f8");
      }

      // crosvm doesn't support ACPI PNP, but QEMU does. We need to disable
      // it on QEMU so that the ISA serial ports aren't claimed by ACPI, so
      // we can use serdev with platform devices instead
      vm_manager_cmdline.push_back("pnpacpi=off");

      // crosvm sets up the ramoops.xx= flags for us, but QEMU does not.
      // See external/crosvm/x86_64/src/lib.rs
      // this feature is not supported on aarch64
      // check guest's /proc/iomem when you need to change mem_address or mem_size
      vm_manager_cmdline.push_back("ramoops.mem_address=0x150000000");
      vm_manager_cmdline.push_back("ramoops.mem_size=0x200000");
      vm_manager_cmdline.push_back("ramoops.console_size=0x80000");
      vm_manager_cmdline.push_back("ramoops.record_size=0x80000");
      vm_manager_cmdline.push_back("ramoops.dump_oops=1");
    }
  }

  if (instance.console() && instance.kgdb()) {
    AppendVector(&vm_manager_cmdline, {"kgdboc_earlycon", "kgdbcon",
                                       "kgdboc=" + instance.console_dev()});
  }
  return vm_manager_cmdline;
}

} // namespace

std::vector<std::string> KernelCommandLineFromConfig(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<std::string> kernel_cmdline;
  AppendVector(&kernel_cmdline, VmManagerKernelCmdline(config, instance));
  AppendVector(&kernel_cmdline, config.extra_kernel_cmdline());
  return kernel_cmdline;
}

} // namespace cuttlefish
