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

#include <glog/logging.h>

#include "common/libs/utils/users.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/crosvm_manager.h"

namespace vm_manager {

VmManager::VmManager(const vsoc::CuttlefishConfig* config)
    : config_(config) {}

namespace{
template <typename T>
VmManager* GetManagerSingleton(const vsoc::CuttlefishConfig* config) {
  static std::shared_ptr<VmManager> vm_manager(new T(config));
  return vm_manager.get();
}
}

std::map<std::string, VmManager::VmManagerHelper>
    VmManager::vm_manager_helpers_ = {
        {
          QemuManager::name(),
          {
            [](const vsoc::CuttlefishConfig* config) {
              return GetManagerSingleton<QemuManager>(config);
            },
            []() { return vsoc::HostSupportsQemuCli(); },
            [](vsoc::CuttlefishConfig* c) {
              return QemuManager::ConfigureBootDevices(c);
            }
          },
        },
        {
          CrosvmManager::name(),
          {
            [](const vsoc::CuttlefishConfig* config) {
              return GetManagerSingleton<CrosvmManager>(config);
            },
            // Same as Qemu for the time being
            []() { return vsoc::HostSupportsQemuCli(); },
            [](vsoc::CuttlefishConfig* c) {
              return CrosvmManager::ConfigureBootDevices(c);
            }
          }
        }
    };

VmManager* VmManager::Get(const std::string& vm_manager_name,
                          const vsoc::CuttlefishConfig* config) {
  if (VmManager::IsValidName(vm_manager_name)) {
    return vm_manager_helpers_[vm_manager_name].builder(config);
  }
  LOG(ERROR) << "Requested invalid VmManager: " << vm_manager_name;
  return nullptr;
}

bool VmManager::IsValidName(const std::string& name) {
  return vm_manager_helpers_.count(name) > 0;
}

bool VmManager::IsVmManagerSupported(const std::string& name) {
  return VmManager::IsValidName(name) &&
         vm_manager_helpers_[name].support_checker();
}

void VmManager::ConfigureBootDevices(vsoc::CuttlefishConfig* config) {
  auto it = vm_manager_helpers_.find(config->vm_manager());
  if (it == vm_manager_helpers_.end()) {
    return;
  }
  it->second.configure_boot_devices(config);
}

std::vector<std::string> VmManager::GetValidNames() {
  std::vector<std::string> ret = {};
  for (const auto& key_val: vm_manager_helpers_) {
    ret.push_back(key_val.first);
  }
  return ret;
}

bool VmManager::UserInGroup(const std::string& group,
                            std::vector<std::string>* config_commands) {
  if (!cvd::InGroup(group)) {
    LOG(ERROR) << "User must be a member of " << group;
    config_commands->push_back("# Add your user to the " + group + " group:");
    config_commands->push_back("sudo usermod -aG " + group + " $USER");
    return false;
  }
  return true;
}

bool VmManager::ValidateHostConfiguration(
    std::vector<std::string>* config_commands) const {
  // the check for cvdnetwork needs to happen even if the user is not in kvm, so
  // we cant just say UserInGroup("kvm") && UserInGroup("cvdnetwork")
  auto in_kvm = VmManager::UserInGroup("kvm", config_commands);
  auto in_cvdnetwork = VmManager::UserInGroup("cvdnetwork", config_commands);
  return in_kvm && in_cvdnetwork;
}
}  // namespace vm_manager
