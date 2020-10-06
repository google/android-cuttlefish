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

#include "host/libs/vm_manager/host_configuration.h"

#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <sys/utsname.h>

#include "common/libs/utils/users.h"

namespace cuttlefish {
namespace vm_manager {
namespace {

bool UserInGroup(const std::string& group,
                 std::vector<std::string>* config_commands) {
  if (!InGroup(group)) {
    LOG(ERROR) << "User must be a member of " << group;
    config_commands->push_back("# Add your user to the " + group + " group:");
    config_commands->push_back("sudo usermod -aG " + group + " $USER");
    return false;
  }
  return true;
}

constexpr std::pair<int,int> invalid_linux_version = std::pair<int,int>();

std::pair<int,int> GetLinuxVersion() {
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

bool LinuxVersionAtLeast(std::vector<std::string>* config_commands,
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

} // namespace

bool ValidateHostConfiguration(std::vector<std::string>* config_commands) {
  // if we can't detect the kernel version, just fail
  auto version = GetLinuxVersion();
  if (version == invalid_linux_version) {
    return false;
  }

  // the check for cvdnetwork needs to happen even if the user is not in kvm, so
  // we can't just say UserInGroup("kvm") && UserInGroup("cvdnetwork")
  auto in_cvdnetwork = UserInGroup("cvdnetwork", config_commands);

  // if we're in the virtaccess group this is likely to be a CrOS environment.
  auto is_cros = InGroup("virtaccess");
  if (is_cros) {
    // relax the minimum kernel requirement slightly, as chromeos-4.4 has the
    // needed backports to enable vhost_vsock
    auto linux_ver_4_4 = LinuxVersionAtLeast(config_commands, version, 4, 4);
    return in_cvdnetwork && linux_ver_4_4;
  } else {
    // this is regular Linux, so use the Debian group name and be more
    // conservative with the kernel version check.
    auto in_kvm = UserInGroup("kvm", config_commands);
    auto linux_ver_4_8 = LinuxVersionAtLeast(config_commands, version, 4, 8);
    return in_cvdnetwork && in_kvm && linux_ver_4_8;
  }
}

} // namespace vm_manager
} // namespace cuttlefish

