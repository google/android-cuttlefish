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
#include <android-base/parseint.h>
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

const char* kCustomActionInstanceID = "instance_id";
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

CustomActionInstanceID GetCustomActionInstanceIDFromJson(
    const Json::Value& dictionary) {
  CustomActionInstanceID config;
  config.instance_id = dictionary[kCustomActionInstanceID].asString();
  return config;
}

CustomShellActionConfig GetCustomShellActionConfigFromJson(
    const Json::Value& dictionary) {
  CustomShellActionConfig config;
  // Shell command with one button.
  Json::Value button_entry = dictionary[kCustomActionButton];
  config.button = {button_entry[kCustomActionButtonCommand].asString(),
    button_entry[kCustomActionButtonTitle].asString(),
    button_entry[kCustomActionButtonIconName].asString()};
  config.shell_command = dictionary[kCustomActionShellCommand].asString();
  return config;
}

CustomActionServerConfig GetCustomActionServerConfigFromJson(
    const Json::Value& dictionary) {
  CustomActionServerConfig config;
  // Action server with possibly multiple buttons.
  for (const Json::Value& button_entry : dictionary[kCustomActionButtons]) {
    config.buttons.push_back(
        {button_entry[kCustomActionButtonCommand].asString(),
        button_entry[kCustomActionButtonTitle].asString(),
        button_entry[kCustomActionButtonIconName].asString()});
  }
  config.server = dictionary[kCustomActionServer].asString();
  return config;
}

CustomDeviceStateActionConfig GetCustomDeviceStateActionConfigFromJson(
    const Json::Value& dictionary) {
  CustomDeviceStateActionConfig config;
  // Device state(s) with one button.
  // Each button press cycles to the next state, then repeats to the first.
  Json::Value button_entry = dictionary[kCustomActionButton];
  config.button = {button_entry[kCustomActionButtonCommand].asString(),
    button_entry[kCustomActionButtonTitle].asString(),
    button_entry[kCustomActionButtonIconName].asString()};
  for (const Json::Value& device_state_entry :
      dictionary[kCustomActionDeviceStates]) {
    DeviceState state;
    if (device_state_entry.isMember(
          kCustomActionDeviceStateLidSwitchOpen)) {
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
  return config;
}

Json::Value ToJson(const CustomActionInstanceID& custom_action) {
  Json::Value json;
  json[kCustomActionInstanceID] = custom_action.instance_id;
  return json;
}

Json::Value ToJson(const CustomShellActionConfig& custom_action) {
  Json::Value json;
  // Shell command with one button.
  json[kCustomActionShellCommand] = custom_action.shell_command;
  json[kCustomActionButton] = Json::Value();
  json[kCustomActionButton][kCustomActionButtonCommand] =
      custom_action.button.command;
  json[kCustomActionButton][kCustomActionButtonTitle] =
      custom_action.button.title;
  json[kCustomActionButton][kCustomActionButtonIconName] =
      custom_action.button.icon_name;
  return json;
}

Json::Value ToJson(const CustomActionServerConfig& custom_action) {
  Json::Value json;
  // Action server with possibly multiple buttons.
  json[kCustomActionServer] = custom_action.server;
  json[kCustomActionButtons] = Json::Value(Json::arrayValue);
  for (const auto& button : custom_action.buttons) {
    Json::Value button_entry;
    button_entry[kCustomActionButtonCommand] = button.command;
    button_entry[kCustomActionButtonTitle] = button.title;
    button_entry[kCustomActionButtonIconName] = button.icon_name;
    json[kCustomActionButtons].append(button_entry);
  }
  return json;
}

Json::Value ToJson(const CustomDeviceStateActionConfig& custom_action) {
  Json::Value json;
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
      custom_action.button.command;
  json[kCustomActionButton][kCustomActionButtonTitle] =
      custom_action.button.title;
  json[kCustomActionButton][kCustomActionButtonIconName] =
      custom_action.button.icon_name;
  return json;
}

std::string DefaultCustomActionConfig() {
  auto custom_action_config_dir =
      DefaultHostArtifactsPath("etc/cvd_custom_action_config");
  if (DirectoryExists(custom_action_config_dir)) {
    auto directory_contents_result =
        DirectoryContents(custom_action_config_dir);
    CHECK(directory_contents_result.ok())
        << directory_contents_result.error().Trace();
    auto custom_action_configs = std::move(*directory_contents_result);
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

int get_instance_order(const std::string& id_str) {
  int instance_index = 0;
  const auto& config = CuttlefishConfig::Get();
  for (const auto& instance : config->Instances()) {
    if (instance.id() == id_str) {
      break;
    }
    instance_index++;
  }
  return instance_index;
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
        [this]() { return custom_action_config_[0]; });
    custom_action_config_flag_.Setter([this](const FlagMatch& match) {
      if (!match.value.empty() &&
          (match.value == "unset" || match.value == "\"unset\"")) {
        custom_action_config_.push_back(DefaultCustomActionConfig());
      } else if (!match.value.empty() && !FileExists(match.value)) {
        LOG(ERROR) << "custom_action_config file \"" << match.value << "\" "
                   << "does not exist.";
        return false;
      } else {
        custom_action_config_.push_back(match.value);
      }
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
      if (match.value == "unset" || match.value == "\"unset\"") {
        AddEmptyJsonCustomActionConfigs();
        return true;
      }
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

  const std::vector<CustomShellActionConfig> CustomShellActions(
      const std::string& id_str = std::string()) const override {
    int instance_index = 0;
    if (instance_actions_.empty()) {
      // No Custom Action input, return empty vector
      return {};
    }

    if (!id_str.empty()) {
      instance_index = get_instance_order(id_str);
    }
    if (instance_index >= instance_actions_.size()) {
      instance_index = 0;
    }
    return instance_actions_[instance_index].custom_shell_actions_;
  }

  const std::vector<CustomActionServerConfig> CustomActionServers(
      const std::string& id_str = std::string()) const override {
    int instance_index = 0;
    if (instance_actions_.empty()) {
      // No Custom Action input, return empty vector
      return {};
    }

    if (!id_str.empty()) {
      instance_index = get_instance_order(id_str);
    }
    if (instance_index >= instance_actions_.size()) {
      instance_index = 0;
    }
    return instance_actions_[instance_index].custom_action_servers_;
  }

  const std::vector<CustomDeviceStateActionConfig> CustomDeviceStateActions(
      const std::string& id_str = std::string()) const override {
    int instance_index = 0;
    if (instance_actions_.empty()) {
      // No Custom Action input, return empty vector
      return {};
    }

    if (!id_str.empty()) {
      instance_index = get_instance_order(id_str);
    }
    if (instance_index >= instance_actions_.size()) {
      instance_index = 0;
    }
    return instance_actions_[instance_index].custom_device_state_actions_;
  }

  // ConfigFragment
  Json::Value Serialize() const override {
    Json::Value actions_array(Json::arrayValue);
    for (const auto& each_instance_actions_ : instance_actions_) {
      actions_array.append(
          ToJson(each_instance_actions_.custom_action_instance_id_));
      for (const auto& action : each_instance_actions_.custom_shell_actions_) {
        actions_array.append(ToJson(action));
      }
      for (const auto& action : each_instance_actions_.custom_action_servers_) {
        actions_array.append(ToJson(action));
      }
      for (const auto& action :
           each_instance_actions_.custom_device_state_actions_) {
        actions_array.append(ToJson(action));
      }
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
    if (!ParseFlags(Flags(), args)) {
      return false;
    }
    if (custom_action_config_.empty()) {
      // no custom action flag input
      custom_action_config_.push_back(DefaultCustomActionConfig());
    }
    for (const auto& config : custom_action_config_) {
      if (config != "") {
        Json::CharReaderBuilder builder;
        std::ifstream ifs(config);
        std::string errorMessage;
        Json::Value custom_action_array(Json::arrayValue);
        if (!Json::parseFromStream(builder, ifs, &custom_action_array,
                                   &errorMessage)) {
          LOG(ERROR) << "Could not read custom actions config file " << config
                     << ": " << errorMessage;
          return false;
        }
        if (!AddJsonCustomActionConfigs(custom_action_array)) {
          return false;
        }
      } else {
        AddEmptyJsonCustomActionConfigs();
      }
    }
    return true;
  }
  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    return WriteGflagsCompatXml(Flags(), out);
  }

 private:
  struct InstanceActions {
    std::vector<CustomShellActionConfig> custom_shell_actions_;
    std::vector<CustomActionServerConfig> custom_action_servers_;
    std::vector<CustomDeviceStateActionConfig> custom_device_state_actions_;
    CustomActionInstanceID custom_action_instance_id_;
  };

  std::vector<Flag> Flags() const {
    return {custom_action_config_flag_, custom_actions_flag_};
  }

  void AddEmptyJsonCustomActionConfigs() {
    InstanceActions instance_action;
    instance_action.custom_action_instance_id_.instance_id =
        std::to_string(instance_actions_.size());
    instance_actions_.push_back(instance_action);
  }

  bool AddJsonCustomActionConfigs(const Json::Value& custom_action_array) {
    if (custom_action_array.type() != Json::arrayValue) {
      LOG(ERROR) << "Expected a JSON array of custom actions";
      return false;
    }
    InstanceActions instance_action;
    instance_action.custom_action_instance_id_.instance_id = "-1";

    for (const auto& custom_action : custom_action_array) {
      // for multi-instances case, assume instance_id, shell_command,
      // server and device_states comes together before next instance
      bool has_instance_id = custom_action.isMember(kCustomActionInstanceID);
      bool has_shell_command =
          custom_action.isMember(kCustomActionShellCommand);
      bool has_server = custom_action.isMember(kCustomActionServer);
      bool has_device_states =
          custom_action.isMember(kCustomActionDeviceStates);
      if (!!has_shell_command + !!has_server + !!has_device_states +
              !!has_instance_id !=
          1) {
        LOG(ERROR) << "Custom action must contain exactly one of "
                      "shell_command, server, device_states or instance_id";
        return false;
      }

      if (has_shell_command) {
        auto config = GetCustomShellActionConfigFromJson(custom_action);
        instance_action.custom_shell_actions_.push_back(config);
      } else if (has_server) {
        auto config = GetCustomActionServerConfigFromJson(custom_action);
        instance_action.custom_action_servers_.push_back(config);
      } else if (has_device_states) {
        auto config = GetCustomDeviceStateActionConfigFromJson(custom_action);
        instance_action.custom_device_state_actions_.push_back(config);
      } else if (has_instance_id) {
        auto config = GetCustomActionInstanceIDFromJson(custom_action);
        if (instance_action.custom_action_instance_id_.instance_id != "-1") {
          // already has instance id, start a new instance
          instance_actions_.push_back(instance_action);
          instance_action = InstanceActions();
        }
        instance_action.custom_action_instance_id_ = config;
      } else {
        LOG(ERROR) << "Unknown custom action type.";
        return false;
      }
    }
    if (instance_action.custom_action_instance_id_.instance_id == "-1") {
      // default id "-1" which means no instance id assigned yet
      // at this time, just assign the # of instance as ID
      instance_action.custom_action_instance_id_.instance_id =
          std::to_string(instance_actions_.size());
    }
    instance_actions_.push_back(instance_action);
    return true;
  }

    ConfigFlag& config_;
    Flag custom_action_config_flag_;
    std::vector<std::string> custom_action_config_;
    Flag custom_actions_flag_;
    std::vector<InstanceActions> instance_actions_;
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
