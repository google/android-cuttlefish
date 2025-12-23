/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <android-base/file.h>
#include "absl/container/btree_map.h"

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"
#include "cuttlefish/host/libs/config/defaults/defaults.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Defaults::Defaults(std::map<std::string, std::string> defaults) {
  for(const auto &[k, v]: defaults) {
    defaults_[k] = v;
  }
}

std::optional<std::string_view> Defaults::Value(std::string_view k) const {
  absl::btree_map<std::string, std::string>::const_iterator it =
      defaults_.find(k);
  if (it == defaults_.end()) {
    return {};
  } else {
    return it->second;
  }
}

std::optional<bool> Defaults::BoolValue(std::string_view k) const {
  return Value(k) == "true" ? true : false;
}

Result<Defaults> Defaults::FromFile(const std::string &path) {
  std::string defaults_str;
  CF_EXPECT(android::base::ReadFileToString(path, &defaults_str),
            "Couldn't read defaults file.");
  std::map<std::string, std::string> defaults_map = CF_EXPECT(
      ParseKeyEqualsValue(defaults_str), "Couldn't parse defaults file.");
  return Defaults(defaults_map);
}

}  // namespace cuttlefish
