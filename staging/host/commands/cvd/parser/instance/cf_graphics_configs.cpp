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
  if (root.isMember("displays")) {
    CF_EXPECT(ValidateDisplaysConfigs(root["displays"]),
              "ValidateDisplaysConfigs fail");
  }

  return {};
}

void InitGraphicsConfigs(Json::Value&) {}

std::vector<std::string> GenerateGraphicsFlags(const Json::Value&) {
  std::vector<std::string> result;

  return result;
}

}  // namespace cuttlefish
