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

#pragma once

#include <map>
#include <optional>

#include <json/json.h>

namespace cuttlefish {
namespace webrtc_streaming {

class ValidationResult {
 public:
  ValidationResult() = default;
  ValidationResult(const std::string &error) : error_(error) {}

  // Helper method to ensure a json object has the required fields convertible
  // to the appropriate types.
  static ValidationResult ValidateJsonObject(
      const Json::Value &obj, const std::string &type,
      const std::map<std::string, Json::ValueType> &required_fields,
      const std::map<std::string, Json::ValueType> &optional_fields = {});

  bool ok() const { return !error_.has_value(); }
  std::string error() const { return error_.value_or(""); }

 private:
  std::optional<std::string> error_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
