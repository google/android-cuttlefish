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

Result<void> InitGraphicsConfigs(Json::Value& instances) {
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "width",
                              CF_DEFAULTS_DISPLAY_WIDTH);
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "height",
                              CF_DEFAULTS_DISPLAY_HEIGHT);
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "dpi",
                              CF_DEFAULTS_DISPLAY_DPI);
  InitIntConfigSubGroupVector(instances, "graphics", "displays",
                              "refresh_rate_hertz",
                              CF_DEFAULTS_DISPLAY_REFRESH_RATE);
  for (auto& instance : instances) {
    CF_EXPECT(InitConfig(instance, CF_DEFAULTS_RECORD_SCREEN,
                         {"graphics", "record_screen"}));
  }
  return {};
}

std::string GenerateDisplayFlag(const Json::Value& instances_json) {
  using google::protobuf::TextFormat;
  cuttlefish::InstancesDisplays all_instances_displays;

  for (const auto& instance_json : instances_json) {
    auto* instance = all_instances_displays.add_instances();
    for (const auto& display_json : instance_json["graphics"]["displays"]) {
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

Result<std::vector<std::string>> GenerateGraphicsFlags(
    const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(GenerateDisplayFlag(instances));
  result.emplace_back(CF_EXPECT(GenerateGflag(instances, "record_screen",
                                              {"graphics", "record_screen"})));
  return result;
}

}  // namespace cuttlefish
