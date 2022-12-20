/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "host/commands/cvd/parser/instance/cf_boot_configs.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <google/protobuf/text_format.h>

#include "launch_cvd.pb.h"

#include "common/libs/utils/base64.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

static std::map<std::string, Json::ValueType> kGraphicsKeyMap = {
    {"displays", Json::ValueType::arrayValue},
};
static std::map<std::string, Json::ValueType> kDisplayKeyMap = {
    {"width", Json::ValueType::intValue},
    {"height", Json::ValueType::intValue},
    {"dpi", Json::ValueType::intValue},
    {"refresh_rate_hertz", Json::ValueType::intValue},
};

Result<void> ValidateDisplaysConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kDisplayKeyMap),
            "ValidateDisplaysConfigs ValidateTypo fail");
  return {};
}

Result<void> ValidateGraphicsConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kGraphicsKeyMap),
            "ValidateGraphicsConfigs ValidateTypo fail");

  if (root.isMember("displays") && root["displays"].size() != 0) {
    int num_displays = root["displays"].size();
    for (int i = 0; i < num_displays; i++) {
      CF_EXPECT(ValidateDisplaysConfigs(root["displays"][i]),
                "ValidateDisplaysConfigs fail");
    }
  }

  return {};
}

void InitGraphicsConfigs(Json::Value& instances) {
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "width",
                              CF_DEFAULTS_DISPLAY_WIDTH);
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "height",
                              CF_DEFAULTS_DISPLAY_HEIGHT);
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "dpi",
                              CF_DEFAULTS_DISPLAY_DPI);
  InitIntConfigSubGroupVector(instances, "graphics", "displays",
                              "refresh_rate_hertz",
                              CF_DEFAULTS_DISPLAY_REFRESH_RATE);
}

std::string GenerateDisplayFlag(const Json::Value& instances_json) {
  using google::protobuf::TextFormat;
  cuttlefish::InstancesDisplays all_instances_displays;

  int num_instances = instances_json.size();
  for (int i = 0; i < num_instances; i++) {
    auto* instance = all_instances_displays.add_instances();
    int num_displays = instances_json[i]["graphics"]["displays"].size();
    for (int j = 0; j < num_displays; j++) {
      Json::Value display_json = instances_json[i]["graphics"]["displays"][j];
      auto* display = instance->add_displays();
      display->set_width(display_json["width"].asInt());
      display->set_height(display_json["height"].asInt());
      display->set_dpi(display_json["dpi"].asInt());
      display->set_refresh_rate_hertz(
          display_json["refresh_rate_hertz"].asInt());
    }
  }

  std::string bin_output;
  if (!all_instances_displays.SerializeToString(&bin_output)) {
    LOG(ERROR) << "Failed to convert display proto to binary string ";
    return std::string();
  }

  std::string base64_output;
  if (!cuttlefish::EncodeBase64((void*)bin_output.c_str(), bin_output.size(),
                                &base64_output)) {
    LOG(ERROR) << "Failed to apply EncodeBase64 to binary string ";
    return std::string();
  }
  return "--displays_binproto=" + base64_output;
}

std::vector<std::string> GenerateGraphicsFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(GenerateDisplayFlag(instances));
  return result;
}

}  // namespace cuttlefish
