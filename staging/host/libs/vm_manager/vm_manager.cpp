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

#include <memory>

#include <android-base/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/crosvm_manager.h"

namespace cuttlefish {
namespace vm_manager {

std::unique_ptr<VmManager> GetVmManager(const std::string& name) {
  std::unique_ptr<VmManager> vmm;
  if (name == QemuManager::name()) {
    vmm.reset(new QemuManager());
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

} // namespace vm_manager
} // namespace cuttlefish

