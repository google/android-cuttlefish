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

#include <android-base/logging.h>
#include <fruit/fruit.h>
#include <iostream>

#include "common/libs/utils/network.h"
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
  bool Setup() override {
    auto used_tap_devices = TapInterfacesInUse();
    if (used_tap_devices.count(instance_.wifi_tap_name())) {
      LOG(ERROR) << "Wifi TAP device already in use";
      return false;
    } else if (used_tap_devices.count(instance_.mobile_tap_name())) {
      LOG(ERROR) << "Mobile TAP device already in use";
      return false;
    } else if (used_tap_devices.count(instance_.ethernet_tap_name())) {
      LOG(ERROR) << "Ethernet TAP device already in use";
      return false;
    }
    return true;
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

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
validationComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, ValidateHostConfigurationFeature>()
      .addMultibinding<SetupFeature, ValidateTapDevices>();
}

}  // namespace cuttlefish
