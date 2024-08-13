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

#include "host/commands/run_cvd/validate.h"

#include <sys/utsname.h>

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/utils/in_sandbox.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/host_configuration.h"

namespace cuttlefish {

static Result<void> TestTapDevices(
    const CuttlefishConfig::InstanceSpecific& instance) {
#ifdef __linux__
  if (InSandbox()) {
    return {};
  }
  auto taps = TapInterfacesInUse();
  auto wifi = instance.wifi_tap_name();
  CF_EXPECTF(taps.count(wifi) == 0, "Device \"{}\" in use", wifi);
  auto mobile = instance.mobile_tap_name();
  CF_EXPECTF(taps.count(mobile) == 0, "Device \"{}\" in use", mobile);
  auto eth = instance.ethernet_tap_name();
  CF_EXPECTF(taps.count(eth) == 0, "Device \"{}\" in use", eth);
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
#ifdef __ANDROID__
  std::vector<std::string> config_commands;
  CF_EXPECTF(vm_manager::ValidateHostConfiguration(&config_commands),
             "Validation of user configuration failed.\n"
             "Execute the following to correctly configure: \n[{}]\n",
             "You may need to logout for the changes to take effect.\n",
             fmt::join(config_commands, "\n"));
#endif
  return {};
}

Result<void> ValidateHostKernel() {
  struct utsname uname_data;
  CF_EXPECT_EQ(uname(&uname_data), 0, "uname failed: " << strerror(errno));
  LOG(DEBUG) << "uts.sysname = \"" << uname_data.sysname << "\"";
  LOG(DEBUG) << "uts.nodename = \"" << uname_data.nodename << "\"";
  LOG(DEBUG) << "uts.release = \"" << uname_data.release << "\"";
  LOG(DEBUG) << "uts.version = \"" << uname_data.version << "\"";
  LOG(DEBUG) << "uts.machine = \"" << uname_data.machine << "\"";
#ifdef _GNU_SOURCE
  LOG(DEBUG) << "uts.domainname = \"" << uname_data.domainname << "\"";
#endif
  return {};
}

}  // namespace cuttlefish
