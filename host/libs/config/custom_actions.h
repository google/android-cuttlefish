/*
 * Copyright (C) 2020 The Android Open Source Project
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
#pragma once

#include <fruit/fruit.h>
#include <optional>
#include <string>
#include <vector>

#include "host/libs/config/config_flag.h"
#include "host/libs/config/config_fragment.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

struct ControlPanelButton {
  std::string command;
  std::string title;
  std::string icon_name;
};

struct DeviceState {
  std::optional<bool> lid_switch_open;
  std::optional<int> hinge_angle_value;
};

struct CustomActionInstanceID {
  std::string instance_id;
};

struct CustomShellActionConfig {
  ControlPanelButton button;
  std::string shell_command;
};

struct CustomActionServerConfig {
  std::string server;
  std::vector<ControlPanelButton> buttons;
};

struct CustomDeviceStateActionConfig {
  ControlPanelButton button;
  std::vector<DeviceState> device_states;
};

class CustomActionConfigProvider : public FlagFeature, public ConfigFragment {
 public:
  virtual const std::vector<CustomShellActionConfig> CustomShellActions(
      const std::string& id_str = std::string()) const = 0;
  virtual const std::vector<CustomActionServerConfig> CustomActionServers(
      const std::string& id_str = std::string()) const = 0;
  virtual const std::vector<CustomDeviceStateActionConfig>
  CustomDeviceStateActions(const std::string& id_str = std::string()) const = 0;
};

fruit::Component<fruit::Required<ConfigFlag>, CustomActionConfigProvider>
CustomActionsComponent();

}  // namespace cuttlefish
