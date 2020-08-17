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

#include "host/frontend/webrtc/lib/utils.h"

#include <map>

#include <json/json.h>

namespace cuttlefish {
namespace webrtc_streaming {

ValidationResult ValidationResult::ValidateJsonObject(
    const Json::Value &obj, const std::string &type,
    const std::map<std::string, Json::ValueType> &fields) {
  for (const auto &field_spec : fields) {
    const auto &field_name = field_spec.first;
    auto field_type = field_spec.second;
    if (!(obj.isMember(field_name) &&
          obj[field_name].isConvertibleTo(field_type))) {
      std::string error_msg = "Expected a field named '";
      error_msg += field_name + "' of type '";
      error_msg += std::to_string(field_type);
      error_msg += "'";
      if (!type.empty()) {
        error_msg += " in message of type '" + type + "'";
      }
      error_msg += ".";
      return {error_msg};
    }
  }
  return {};
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
