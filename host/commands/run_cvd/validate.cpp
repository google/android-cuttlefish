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

#include <sys/utsname.h>

#include <iostream>

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/utils/network.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/vm_manager/host_configuration.h"

namespace cuttlefish {
namespace {

using vm_manager::ValidateHostConfiguration;

class ValidateTapDevices : public SetupFeature {
 public:
  INJECT(ValidateTapDevices(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  std::string Name() const override { return "ValidateTapDevices"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    auto taps = TapInterfacesInUse();
    auto wifi = instance_.wifi_tap_name();
    CF_EXPECT(taps.count(wifi) == 0, "Device \"" << wifi << "\" in use");
    auto mobile = instance_.mobile_tap_name();
    CF_EXPECT(taps.count(mobile) == 0, "Device \"" << mobile << "\" in use");
    auto eth = instance_.ethernet_tap_name();
    CF_EXPECT(taps.count(eth) == 0, "Device \"" << eth << "\" in use");
    return {};
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class ValidateHostConfigurationFeature : public SetupFeature {
 public:
  INJECT(ValidateHostConfigurationFeature()) {}

  bool Enabled() const override {
#ifndef __ANDROID__
    return true;
#else
    return false;
#endif
  }
  std::string Name() const override { return "ValidateHostConfiguration"; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override {
    // Check host configuration
    std::vector<std::string> config_commands;
    if (!ValidateHostConfiguration(&config_commands)) {
      LOG(ERROR) << "Validation of user configuration failed";
      std::cout << "Execute the following to correctly configure:" << std::endl;
      for (auto& command : config_commands) {
        std::cout << "  " << command << std::endl;
      }
      std::cout << "You may need to logout for the changes to take effect"
                << std::endl;
      return false;
    }
    return true;
  }
};

class ValidateHostKernelFeature : public SetupFeature {
 public:
  INJECT(ValidateHostKernelFeature()) {}

  bool Enabled() const override { return true; }
  std::string Name() const override { return "ValidateHostKernel"; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
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
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
validationComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, ValidateHostConfigurationFeature>()
      .addMultibinding<SetupFeature, ValidateHostKernelFeature>()
      .addMultibinding<SetupFeature, ValidateTapDevices>();
}

}  // namespace cuttlefish
