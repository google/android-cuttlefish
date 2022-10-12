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
#include "host/commands/cvd/parser/cf_configs_common.h"

#include <android-base/logging.h>
namespace cuttlefish {

/**
 * Validate Json data Name and type
 */
bool ValidateTypo(const Json::Value& root,
                  const std::map<std::string, Json::ValueType>& map) {
  for (const std::string& flag : root.getMemberNames()) {
    if (map.count(flag) == 0) {
      LOG(WARNING) << "Invalid flag name (typo) , Param --> " << flag
                   << " not recognized";
      return false;
    }
    if (!root[flag].isConvertibleTo(map.at(flag))) {
      LOG(WARNING) << "Invalid flag type " << flag;
      return false;
    }
  }
  return true;
}

}  // namespace cuttlefish