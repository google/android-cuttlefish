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

#include "cuttlefish/host/commands/run_cvd/validate.h"

#include <errno.h>
#include <sys/utsname.h>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "fmt/ranges.h"

#include "allocd/alloc_utils.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/vm_manager/host_configuration.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

static Result<void> TestTapDevices(
    const CuttlefishConfig::InstanceSpecific& instance) {
#ifdef __linux__
  if (InSandbox()) {
    return {};
  }
  auto wifi = instance.wifi_tap_name();
  CF_EXPECTF(ValidateTapInterfaceIsUsable(wifi), "Device \"{}\" in use", wifi);
  auto mobile = instance.mobile_tap_name();
  CF_EXPECTF(ValidateTapInterfaceIsUsable(mobile), "Device \"{}\" in use",
             mobile);
  auto eth = instance.ethernet_tap_name();
  CF_EXPECTF(ValidateTapInterfaceIsUsable(eth), "Device \"{}\" in use", eth);
#else
  (void)instance;
#endif
  return {};
}

Result<void> ValidateTapDevices(
    const CuttlefishConfig::InstanceSpecific& instance) {
  CF_EXPECT(TestTapDevices(instance),
            "There appears to be another cuttlefish device"
            " already running, using the requested host "
            "resources. Try `cvd reset` or `pkill run_cvd` "
            "and `pkill crosvm`");
  return {};
}

Result<void> ValidateHostConfiguration() {
  std::vector<vm_manager::HostConfigurationAction> actions =
      CF_EXPECT(vm_manager::ValidateHostConfiguration());
  if (actions.empty()) {
    return {};
  }
  std::vector<std::string> error_msgs;
  for (const vm_manager::HostConfigurationAction& action : actions) {
    if (!action.description.empty()) {
      error_msgs.push_back("# " + action.description);
    }
    if (!action.command.empty()) {
      error_msgs.push_back(absl::StrJoin(action.command, " "));
    }
  }
  return CF_ERRF(
      "Validation of user configuration failed.\n"
      "Execute the following to correctly configure: \n[{}]\n",
      fmt::join(error_msgs, "\n"));
}

Result<void> ValidateHostKernel() {
  struct utsname uname_data;
  CF_EXPECT_EQ(uname(&uname_data), 0, "uname failed: " << StrError(errno));
  VLOG(0) << "uts.sysname = \"" << uname_data.sysname << "\"";
  VLOG(0) << "uts.nodename = \"" << uname_data.nodename << "\"";
  VLOG(0) << "uts.release = \"" << uname_data.release << "\"";
  VLOG(0) << "uts.version = \"" << uname_data.version << "\"";
  VLOG(0) << "uts.machine = \"" << uname_data.machine << "\"";
#ifdef _GNU_SOURCE
  VLOG(0) << "uts.domainname = \"" << uname_data.domainname << "\"";
#endif
  return {};
}

}  // namespace cuttlefish
