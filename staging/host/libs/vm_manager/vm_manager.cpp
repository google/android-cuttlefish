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

#include "host/libs/vm_manager/vm_manager.h"

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include <iomanip>
#include <memory>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/gem5_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace vm_manager {

std::unique_ptr<VmManager> GetVmManager(const std::string& name, Arch arch) {
  std::unique_ptr<VmManager> vmm;
  if (name == QemuManager::name()) {
    vmm.reset(new QemuManager(arch));
  } else if (name == Gem5Manager::name()) {
    vmm.reset(new Gem5Manager(arch));
  } else if (name == CrosvmManager::name()) {
    vmm.reset(new CrosvmManager());
  }
  if (!vmm) {
    LOG(ERROR) << "Invalid VM manager: " << name;
    return {};
  }
  if (!vmm->IsSupported()) {
    LOG(ERROR) << "VM manager " << name << " is not supported on this machine.";
    return {};
  }
  return vmm;
}

std::string ConfigureMultipleBootDevices(const std::string& pci_path,
                                         int pci_offset, int num_disks) {
  int num_boot_devices =
      (num_disks < VmManager::kDefaultNumBootDevices) ? num_disks : VmManager::kDefaultNumBootDevices;
  std::string boot_devices_prop = "androidboot.boot_devices=";
  for (int i = 0; i < num_boot_devices; i++) {
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(2) << std::hex
           << pci_offset + i + VmManager::kDefaultNumHvcs + VmManager::kMaxDisks - num_disks;
    boot_devices_prop += pci_path + stream.str() + ".0,";
  }
  boot_devices_prop.pop_back();
  return {boot_devices_prop};
}

fruit::Component<fruit::Required<const CuttlefishConfig>, VmManager>
VmManagerComponent() {
  return fruit::createComponent().registerProvider(
      [](const CuttlefishConfig& config) {
        auto vmm = GetVmManager(config.vm_manager(), config.target_arch());
        CHECK(vmm) << "Invalid VMM/Arch: \"" << config.vm_manager() << "\""
                   << (int)config.target_arch() << "\"";
        return vmm.release();  // fruit takes ownership of raw pointers
      });
}

} // namespace vm_manager
} // namespace cuttlefish
