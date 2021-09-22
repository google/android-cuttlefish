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
#include <sys/utsname.h>

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
            GetManagerSingleton<QemuManager>,
            vsoc::HostSupportsQemuCli,
            QemuManager::ConfigureGpu,
            QemuManager::ConfigureBootDevices,
          },
        },
        {
          CrosvmManager::name(),
          {
            GetManagerSingleton<CrosvmManager>,
            // Same as Qemu for the time being
            vsoc::HostSupportsQemuCli,
            CrosvmManager::ConfigureGpu,
            CrosvmManager::ConfigureBootDevices,
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

std::vector<std::string> VmManager::ConfigureGpuMode(
    const std::string& vmm_name, const std::string& gpu_mode) {
  auto it = vm_manager_helpers_.find(vmm_name);
  if (it == vm_manager_helpers_.end()) {
    return {};
  }
  return it->second.configure_gpu_mode(gpu_mode);
}

std::vector<std::string> VmManager::ConfigureBootDevices(
    const std::string& vmm_name) {
  auto it = vm_manager_helpers_.find(vmm_name);
  if (it == vm_manager_helpers_.end()) {
    return {};
  }
  return it->second.configure_boot_devices();
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

std::pair<int,int> VmManager::GetLinuxVersion() {
  struct utsname info;
  if (!uname(&info)) {
    char* digit = strtok(info.release, "+.-");
    int major = atoi(digit);
    if (digit) {
      digit = strtok(NULL, "+.-");
      int minor = atoi(digit);
      return std::pair<int,int>{major, minor};
    }
  }
  LOG(ERROR) << "Failed to detect Linux kernel version";
  return invalid_linux_version;
}

bool VmManager::LinuxVersionAtLeast(std::vector<std::string>* config_commands,
                                    const std::pair<int,int>& version,
                                    int major, int minor) {
  if (version.first > major ||
      (version.first == major && version.second >= minor)) {
    return true;
  }

  LOG(ERROR) << "Kernel version must be >=" << major << "." << minor
             << ", have " << version.first << "." << version.second;
  config_commands->push_back("# Please upgrade your kernel to >=" +
                             std::to_string(major) + "." +
                             std::to_string(minor));
  return false;
}

bool VmManager::ValidateHostConfiguration(
    std::vector<std::string>* config_commands) const {
  // if we can't detect the kernel version, just fail
  auto version = VmManager::GetLinuxVersion();
  if (version == invalid_linux_version) {
    return false;
  }

  // the check for cvdnetwork needs to happen even if the user is not in kvm, so
  // we can't just say UserInGroup("kvm") && UserInGroup("cvdnetwork")
  auto in_cvdnetwork = VmManager::UserInGroup("cvdnetwork", config_commands);

  // if we're in the virtaccess group this is likely to be a CrOS environment.
  auto is_cros = cvd::InGroup("virtaccess");
  if (is_cros) {
    // relax the minimum kernel requirement slightly, as chromeos-4.4 has the
    // needed backports to enable vhost_vsock
    auto linux_ver_4_4 =
      VmManager::LinuxVersionAtLeast(config_commands, version, 4, 4);
    return in_cvdnetwork && linux_ver_4_4;
  } else {
    // this is regular Linux, so use the Debian group name and be more
    // conservative with the kernel version check.
    auto in_kvm = VmManager::UserInGroup("kvm", config_commands);
    auto linux_ver_4_8 =
      VmManager::LinuxVersionAtLeast(config_commands, version, 4, 8);
    return in_cvdnetwork && in_kvm && linux_ver_4_8;
  }
}

void VmManager::WithFrontend(bool enabled) {
  frontend_enabled_ = enabled;
}

void VmManager::WithKernelCommandLine(const std::string& kernel_cmdline) {
  kernel_cmdline_ = kernel_cmdline;
}

}  // namespace vm_manager
