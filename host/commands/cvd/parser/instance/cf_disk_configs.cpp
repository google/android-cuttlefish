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
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/libs/config/cuttlefish_config.h"

#define DEFAULT_BLANK_DATA_IMAGE_SIZE "unset"

namespace cuttlefish {

void InitDiskConfigs(Json::Value& instances) {
  InitStringConfig(instances, "disk", "blank_data_image_mb",
                   DEFAULT_BLANK_DATA_IMAGE_SIZE);
}

std::vector<std::string> GenerateDiskFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(GenerateGflag(instances, "blank_data_image_mb", "disk",
                                    "blank_data_image_mb"));
  return result;
}

}  // namespace cuttlefish
