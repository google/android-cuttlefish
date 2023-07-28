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
#include "host/commands/cvd/parser/instance/cf_disk_configs.h"

#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

#define DEFAULT_BLANK_DATA_IMAGE_SIZE "unset"

namespace cuttlefish {

Result<void> InitDiskConfigs(Json::Value& instances) {
  const int size = instances.size();
  for (int i = 0; i < size; i++) {
    CF_EXPECT(InitConfig(instances[i], DEFAULT_BLANK_DATA_IMAGE_SIZE,
                         {"disk", "blank_data_image_mb"}));
  }
  return {};
}

Result<std::vector<std::string>> GenerateDiskFlags(
    const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(CF_EXPECT(GenerateGflag(
      instances, "blank_data_image_mb", {"disk", "blank_data_image_mb"})));
  return result;
}

}  // namespace cuttlefish
