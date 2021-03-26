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
#include "host/libs/config/custom_actions.h"

#include <android-base/logging.h>
#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

const char* kCustomActionShellCommand = "shell_command";
const char* kCustomActionServer = "server";
const char* kCustomActionDeviceStates = "device_states";
const char* kCustomActionDeviceStateLidSwitchOpen = "lid_switch_open";
const char* kCustomActionDeviceStateHingeAngleValue = "hinge_angle_value";
const char* kCustomActionButton = "button";
const char* kCustomActionButtons = "buttons";
const char* kCustomActionButtonCommand = "command";
const char* kCustomActionButtonTitle = "title";
const char* kCustomActionButtonIconName = "icon_name";

} //namespace


CustomActionConfig::CustomActionConfig(const Json::Value& dictionary) {
  if (dictionary.isMember(kCustomActionShellCommand) +
          dictionary.isMember(kCustomActionServer) +
          dictionary.isMember(kCustomActionDeviceStates) !=
      1) {
    LOG(FATAL) << "Custom action must contain exactly one of shell_command, "
               << "server, or device_states";
    return;
  }
  if (dictionary.isMember(kCustomActionShellCommand)) {
    // Shell command with one button.
    Json::Value button_entry = dictionary[kCustomActionButton];
    buttons = {{button_entry[kCustomActionButtonCommand].asString(),
                button_entry[kCustomActionButtonTitle].asString(),
                button_entry[kCustomActionButtonIconName].asString()}};
    shell_command = dictionary[kCustomActionShellCommand].asString();
  } else if (dictionary.isMember(kCustomActionServer)) {
    // Action server with possibly multiple buttons.
    for (const Json::Value& button_entry : dictionary[kCustomActionButtons]) {
      ControlPanelButton button = {
          button_entry[kCustomActionButtonCommand].asString(),
          button_entry[kCustomActionButtonTitle].asString(),
          button_entry[kCustomActionButtonIconName].asString()};
      buttons.push_back(button);
    }
    server = dictionary[kCustomActionServer].asString();
  } else if (dictionary.isMember(kCustomActionDeviceStates)) {
    // Device state(s) with one button.
    // Each button press cycles to the next state, then repeats to the first.
    Json::Value button_entry = dictionary[kCustomActionButton];
    buttons = {{button_entry[kCustomActionButtonCommand].asString(),
                button_entry[kCustomActionButtonTitle].asString(),
                button_entry[kCustomActionButtonIconName].asString()}};
    for (const Json::Value& device_state_entry :
         dictionary[kCustomActionDeviceStates]) {
      DeviceState state;
      if (device_state_entry.isMember(kCustomActionDeviceStateLidSwitchOpen)) {
        state.lid_switch_open =
            device_state_entry[kCustomActionDeviceStateLidSwitchOpen].asBool();
      }
      if (device_state_entry.isMember(
              kCustomActionDeviceStateHingeAngleValue)) {
        state.hinge_angle_value =
            device_state_entry[kCustomActionDeviceStateHingeAngleValue].asInt();
      }
      device_states.push_back(state);
    }
  } else {
    LOG(FATAL) << "Unknown custom action type.";
  }
}

Json::Value CustomActionConfig::ToJson() const {
  Json::Value custom_action;
  if (shell_command) {
    // Shell command with one button.
    custom_action[kCustomActionShellCommand] = *shell_command;
    custom_action[kCustomActionButton] = Json::Value();
    custom_action[kCustomActionButton][kCustomActionButtonCommand] =
        buttons[0].command;
    custom_action[kCustomActionButton][kCustomActionButtonTitle] =
        buttons[0].title;
    custom_action[kCustomActionButton][kCustomActionButtonIconName] =
        buttons[0].icon_name;
  } else if (server) {
    // Action server with possibly multiple buttons.
    custom_action[kCustomActionServer] = *server;
    custom_action[kCustomActionButtons] = Json::Value(Json::arrayValue);
    for (const auto& button : buttons) {
      Json::Value button_entry;
      button_entry[kCustomActionButtonCommand] = button.command;
      button_entry[kCustomActionButtonTitle] = button.title;
      button_entry[kCustomActionButtonIconName] = button.icon_name;
      custom_action[kCustomActionButtons].append(button_entry);
    }
  } else if (!device_states.empty()) {
    // Device state(s) with one button.
    custom_action[kCustomActionDeviceStates] = Json::Value(Json::arrayValue);
    for (const auto& device_state : device_states) {
      Json::Value device_state_entry;
      if (device_state.lid_switch_open) {
        device_state_entry[kCustomActionDeviceStateLidSwitchOpen] =
            *device_state.lid_switch_open;
      }
      if (device_state.hinge_angle_value) {
        device_state_entry[kCustomActionDeviceStateHingeAngleValue] =
            *device_state.hinge_angle_value;
      }
      custom_action[kCustomActionDeviceStates].append(device_state_entry);
    }
    custom_action[kCustomActionButton] = Json::Value();
    custom_action[kCustomActionButton][kCustomActionButtonCommand] =
        buttons[0].command;
    custom_action[kCustomActionButton][kCustomActionButtonTitle] =
        buttons[0].title;
    custom_action[kCustomActionButton][kCustomActionButtonIconName] =
        buttons[0].icon_name;
  } else {
    LOG(FATAL) << "Unknown custom action type.";
  }
  return custom_action;
}

}  // namespace cuttlefish
