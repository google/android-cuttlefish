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
#include <android-base/strings.h>
#include <json/json.h>

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
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

std::optional<CustomActionConfig> CustomActionConfigFromJson(
    const Json::Value& dictionary) {
  bool has_shell_command = dictionary.isMember(kCustomActionShellCommand);
  bool has_server = dictionary.isMember(kCustomActionServer);
  bool has_device_states = dictionary.isMember(kCustomActionDeviceStates);
  if (!!has_shell_command + !!has_server + !!has_device_states != 1) {
    LOG(ERROR) << "Custom action must contain exactly one of shell_command, "
               << "server, or device_states";
    return {};
  }
  CustomActionConfig config;
  if (has_shell_command) {
    // Shell command with one button.
    Json::Value button_entry = dictionary[kCustomActionButton];
    config.buttons = {{button_entry[kCustomActionButtonCommand].asString(),
                       button_entry[kCustomActionButtonTitle].asString(),
                       button_entry[kCustomActionButtonIconName].asString()}};
    config.shell_command = dictionary[kCustomActionShellCommand].asString();
  } else if (has_server) {
    // Action server with possibly multiple buttons.
    for (const Json::Value& button_entry : dictionary[kCustomActionButtons]) {
      ControlPanelButton button = {
          button_entry[kCustomActionButtonCommand].asString(),
          button_entry[kCustomActionButtonTitle].asString(),
          button_entry[kCustomActionButtonIconName].asString()};
      config.buttons.push_back(button);
    }
    config.server = dictionary[kCustomActionServer].asString();
  } else if (has_device_states) {
    // Device state(s) with one button.
    // Each button press cycles to the next state, then repeats to the first.
    Json::Value button_entry = dictionary[kCustomActionButton];
    config.buttons = {{button_entry[kCustomActionButtonCommand].asString(),
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
      config.device_states.push_back(state);
    }
  } else {
    LOG(ERROR) << "Unknown custom action type.";
    return {};
  }
  return config;
}

Json::Value ToJson(const CustomActionConfig& custom_action) {
  Json::Value json;
  if (custom_action.shell_command) {
    // Shell command with one button.
    json[kCustomActionShellCommand] = *custom_action.shell_command;
    json[kCustomActionButton] = Json::Value();
    json[kCustomActionButton][kCustomActionButtonCommand] =
        custom_action.buttons[0].command;
    json[kCustomActionButton][kCustomActionButtonTitle] =
        custom_action.buttons[0].title;
    json[kCustomActionButton][kCustomActionButtonIconName] =
        custom_action.buttons[0].icon_name;
  } else if (custom_action.server) {
    // Action server with possibly multiple buttons.
    json[kCustomActionServer] = *custom_action.server;
    json[kCustomActionButtons] = Json::Value(Json::arrayValue);
    for (const auto& button : custom_action.buttons) {
      Json::Value button_entry;
      button_entry[kCustomActionButtonCommand] = button.command;
      button_entry[kCustomActionButtonTitle] = button.title;
      button_entry[kCustomActionButtonIconName] = button.icon_name;
      json[kCustomActionButtons].append(button_entry);
    }
  } else if (!custom_action.device_states.empty()) {
    // Device state(s) with one button.
    json[kCustomActionDeviceStates] = Json::Value(Json::arrayValue);
    for (const auto& device_state : custom_action.device_states) {
      Json::Value device_state_entry;
      if (device_state.lid_switch_open) {
        device_state_entry[kCustomActionDeviceStateLidSwitchOpen] =
            *device_state.lid_switch_open;
      }
      if (device_state.hinge_angle_value) {
        device_state_entry[kCustomActionDeviceStateHingeAngleValue] =
            *device_state.hinge_angle_value;
      }
      json[kCustomActionDeviceStates].append(device_state_entry);
    }
    json[kCustomActionButton] = Json::Value();
    json[kCustomActionButton][kCustomActionButtonCommand] =
        custom_action.buttons[0].command;
    json[kCustomActionButton][kCustomActionButtonTitle] =
        custom_action.buttons[0].title;
    json[kCustomActionButton][kCustomActionButtonIconName] =
        custom_action.buttons[0].icon_name;
  } else {
    LOG(FATAL) << "Unknown custom action type.";
  }
  return json;
}

std::string DefaultCustomActionConfig() {
  auto custom_action_config_dir =
      DefaultHostArtifactsPath("etc/cvd_custom_action_config");
  if (DirectoryExists(custom_action_config_dir)) {
    auto custom_action_configs = DirectoryContents(custom_action_config_dir);
    // Two entries are always . and ..
    if (custom_action_configs.size() > 3) {
      LOG(ERROR) << "Expected at most one custom action config in "
                 << custom_action_config_dir << ". Please delete extras.";
    } else if (custom_action_configs.size() == 3) {
      for (const auto& config : custom_action_configs) {
        if (android::base::EndsWithIgnoreCase(config, ".json")) {
          return custom_action_config_dir + "/" + config;
        }
      }
    }
  }
  return "";
}

class CustomActionConfigImpl : public CustomActionConfigProvider {
 public:
  INJECT(CustomActionConfigImpl(ConfigFlag& config)) : config_(config) {
    custom_action_config_flag_ = GflagsCompatFlag("custom_action_config");
    custom_action_config_flag_.Help(
        "Path to a custom action config JSON. Defaults to the file provided by "
        "build variable CVD_CUSTOM_ACTION_CONFIG. If this build variable is "
        "empty then the custom action config will be empty as well.");
    custom_action_config_flag_.Getter(
        [this]() { return custom_action_config_; });
    custom_action_config_flag_.Setter([this](const FlagMatch& match) {
      if (!match.value.empty() && !FileExists(match.value)) {
        LOG(ERROR) << "custom_action_config file \"" << match.value << "\" "
                   << "does not exist.";
        return false;
      }
      custom_action_config_ = match.value;
      return true;
    });
    // TODO(schuffelen): Access ConfigFlag directly for these values.
    custom_actions_flag_ = GflagsCompatFlag("custom_actions");
    custom_actions_flag_.Help(
        "Serialized JSON of an array of custom action objects (in the same "
        "format as custom action config JSON files). For use within --config "
        "preset config files; prefer --custom_action_config to specify a "
        "custom config file on the command line. Actions in this flag are "
        "combined with actions in --custom_action_config.");
    custom_actions_flag_.Setter([this](const FlagMatch& match) {
      // Load the custom action from the --config preset file.
      Json::CharReaderBuilder builder;
      std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
      std::string errorMessage;
      Json::Value custom_action_array(Json::arrayValue);
      if (!reader->parse(&*match.value.begin(), &*match.value.end(),
                         &custom_action_array, &errorMessage)) {
        LOG(ERROR) << "Could not read custom actions config flag: "
                   << errorMessage;
        return false;
      }
      return AddJsonCustomActionConfigs(custom_action_array);
    });
  }

  const std::vector<CustomActionConfig>& CustomActions() const override {
    return custom_actions_;
  }

  // ConfigFragment
  Json::Value Serialize() const override {
    Json::Value actions_array(Json::arrayValue);
    for (const auto& action : CustomActions()) {
      actions_array.append(ToJson(action));
    }
    return actions_array;
  }
  bool Deserialize(const Json::Value& custom_actions_json) override {
    return AddJsonCustomActionConfigs(custom_actions_json);
  }

  // FlagFeature
  std::string Name() const override { return "CustomActionConfig"; }
  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_)};
  }

  bool Process(std::vector<std::string>& args) override {
    custom_action_config_ = DefaultCustomActionConfig();
    if (!ParseFlags(Flags(), args)) {
      return false;
    }
    if (custom_action_config_ != "") {
      Json::CharReaderBuilder builder;
      std::ifstream ifs(custom_action_config_);
      std::string errorMessage;
      Json::Value custom_action_array(Json::arrayValue);
      if (!Json::parseFromStream(builder, ifs, &custom_action_array,
                                 &errorMessage)) {
        LOG(ERROR) << "Could not read custom actions config file "
                   << custom_action_config_ << ": " << errorMessage;
        return false;
      }
      return AddJsonCustomActionConfigs(custom_action_array);
    }
    return true;
  }
  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    return WriteGflagsCompatXml(Flags(), out);
  }

 private:
  std::vector<Flag> Flags() const {
    return {custom_action_config_flag_, custom_actions_flag_};
  }

  bool AddJsonCustomActionConfigs(const Json::Value& custom_action_array) {
    if (custom_action_array.type() != Json::arrayValue) {
      LOG(ERROR) << "Expected a JSON array of custom actions";
      return false;
    }
    for (const auto& custom_action_json : custom_action_array) {
      auto custom_action = CustomActionConfigFromJson(custom_action_json);
      if (custom_action) {
        custom_actions_.push_back(*custom_action);
      } else {
        LOG(ERROR) << "Validation failed on a custom action";
        return false;
      }
    }
    return true;
  }

  ConfigFlag& config_;
  Flag custom_action_config_flag_;
  std::string custom_action_config_;
  Flag custom_actions_flag_;
  std::vector<CustomActionConfig> custom_actions_;
};

}  // namespace

fruit::Component<fruit::Required<ConfigFlag>, CustomActionConfigProvider>
CustomActionsComponent() {
  return fruit::createComponent()
      .bind<CustomActionConfigProvider, CustomActionConfigImpl>()
      .addMultibinding<ConfigFragment, CustomActionConfigProvider>()
      .addMultibinding<FlagFeature, CustomActionConfigProvider>();
}

}  // namespace cuttlefish
