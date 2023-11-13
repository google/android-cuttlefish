/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include "host/commands/cvd/parser/selector_parser.h"

#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {

Result<std::vector<std::string>> ParseSelectorConfigs(Json::Value& root) {
  std::string instance_name_flag =
      CF_EXPECT(GenerateGflag(root["instances"], "instance_name", {"name"}));

  if (!HasValue(root, {"common", "group_name"})) {
    return {{instance_name_flag}};
  }

  return {{instance_name_flag,
           GenerateGflag("group_name", {CF_EXPECT(GetValue<std::string>(
                                           root, {"common", "group_name"}))})}};
}

}  // namespace cuttlefish
