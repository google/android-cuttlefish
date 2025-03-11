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

#include "host/frontend/webrtc/libcommon/utils.h"

#include <map>

#include <json/json.h>

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

Result<void> ValidateField(const Json::Value &obj, const std::string &type,
                           const std::string &field_name,
                           const Json::ValueType &field_type, bool required) {
  CF_EXPECT(obj.isObject(), "Expected object with name-value pairs");
  if (!obj.isMember(field_name) && !required) {
    return {};
  }
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
    return CF_ERR(error_msg);
  }
  return {};
}

}  // namespace

Result<void> ValidateJsonObject(
    const Json::Value &obj, const std::string &type,
    const std::map<std::string, Json::ValueType> &required_fields,
    const std::map<std::string, Json::ValueType> &optional_fields) {
  for (const auto &field_spec : required_fields) {
    CF_EXPECT(
        ValidateField(obj, type, field_spec.first, field_spec.second, true));
  }
  for (const auto &field_spec : optional_fields) {
    CF_EXPECT(
        ValidateField(obj, type, field_spec.first, field_spec.second, false));
  }
  return {};
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
