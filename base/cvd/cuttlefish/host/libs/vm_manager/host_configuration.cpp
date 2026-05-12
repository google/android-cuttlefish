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

#include "cuttlefish/host/libs/vm_manager/host_configuration.h"

#include <sys/utsname.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/utils/container.h"
#include "cuttlefish/common/libs/utils/users.h"

namespace cuttlefish {
namespace vm_manager {
namespace {

template <typename T>
void PushOptional(std::vector<T>& vec, std::optional<T> opt) {
  if (opt) {
    vec.push_back(std::move(*opt));
  }
}

Result<std::optional<HostConfigurationAction>> PutUserInGroup(
    const std::string& group) {
  if (InGroup(group)) {
    return std::nullopt;
  }
  std::string username =
      CF_EXPECT(CurrentUserName(), "Failed to get current username");
  return HostConfigurationAction{
      .command = {"sudo", "usermod", "-aG", group, username},
      .description = "Add your user to the " + group + " group",
  };
}

Result<std::pair<int, int>> GetLinuxVersion() {
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
  return CF_ERR("Failed to detect Linux kernel version");
}

Result<std::optional<HostConfigurationAction>> EnforceLinuxVersionAtLeast(
    const std::pair<int, int>& version, int major, int minor) {
  if (version.first > major ||
      (version.first == major && version.second >= minor)) {
    return std::nullopt;
  }

  return HostConfigurationAction{
      .command = {},
      .description = "Please upgrade your kernel to >= " +
                     std::to_string(major) + "." + std::to_string(minor),
  };
}

} // namespace

Result<std::vector<HostConfigurationAction>> ValidateHostConfiguration() {
  std::vector<HostConfigurationAction> actions;
#ifndef __APPLE__
  // if we can't detect the kernel version, just fail
  std::pair<int, int> version = CF_EXPECT(GetLinuxVersion());

  // Checking host configuration via GID or group name on rootless container
  // instance is hard as user namespace is separated from the host.
  if (IsRunningInContainer()) {
    // TODO(seungjaeyoo): Validate access on actual resources like devices
    // rather than group name, which is applicable for all host environments
    // including container.
    PushOptional(actions, CF_EXPECT(EnforceLinuxVersionAtLeast(version, 4, 8)));
    return actions;
  }

  // the check for cvdnetwork needs to happen even if the user is not in kvm, so
  // we can't just say UserInGroup("kvm") && UserInGroup("cvdnetwork")
  PushOptional(actions, CF_EXPECT(PutUserInGroup("cvdnetwork")));

  // if we're in the virtaccess group this is likely to be a CrOS environment.
  bool is_cros = InGroup("virtaccess");
  if (is_cros) {
    // relax the minimum kernel requirement slightly, as chromeos-4.4 has the
    // needed backports to enable vhost_vsock
    PushOptional(actions, CF_EXPECT(EnforceLinuxVersionAtLeast(version, 4, 4)));
  } else {
    // this is regular Linux, so use the Debian group name and be more
    // conservative with the kernel version check.
    PushOptional(actions, CF_EXPECT(PutUserInGroup("kvm")));
    PushOptional(actions, CF_EXPECT(EnforceLinuxVersionAtLeast(version, 4, 8)));
  }
#endif
  return actions;
}

} // namespace vm_manager
} // namespace cuttlefish

