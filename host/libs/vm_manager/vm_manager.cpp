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
#include "host/libs/vm_manager/libvirt_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace vm_manager {

VmManager::VmManager(vsoc::CuttlefishConfig* config)
    : config_(config) {}

namespace{
template <typename T>
VmManager* GetManagerSingleton(vsoc::CuttlefishConfig* config) {
  static std::shared_ptr<VmManager> vm_manager(new T(config));
  return vm_manager.get();
}
}

std::map<std::string, VmManager::VmManagerHelper>
    VmManager::vm_manager_helpers_ = {
        {LibvirtManager::name(),
         {[](vsoc::CuttlefishConfig* config) {
            return GetManagerSingleton<LibvirtManager>(config);
          },
          []() { return true; }}},
        {QemuManager::name(),
         {[](vsoc::CuttlefishConfig* config) {
            return GetManagerSingleton<QemuManager>(config);
          },
          []() { return vsoc::HostSupportsQemuCli(); }}}};

VmManager* VmManager::Get(const std::string& vm_manager_name,
                          vsoc::CuttlefishConfig* config) {
  if (VmManager::IsValidName(vm_manager_name)) {
    return vm_manager_helpers_[vm_manager_name].first(config);
  }
  LOG(ERROR) << "Requested invalid VmManager: " << vm_manager_name;
  return nullptr;
}

bool VmManager::IsValidName(const std::string& name) {
  return vm_manager_helpers_.count(name) > 0;
}

bool VmManager::IsVmManagerSupported(const std::string& name) {
  return VmManager::IsValidName(name) &&
         vm_manager_helpers_[name].second();
}

std::vector<std::string> VmManager::GetValidNames() {
  std::vector<std::string> ret = {};
  for (auto key_val: vm_manager_helpers_) {
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
}  // namespace vm_manager
