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
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/btree_map.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class Defaults {
 public:
  Defaults() : defaults_() {};
  Defaults(std::map<std::string, std::string, std::less<void>> defaults);
  static Result<Defaults> FromFile(const std::string &path);

  std::optional<std::string_view> Value(std::string_view k) const;
  std::optional<bool> BoolValue(std::string_view k) const;

 private:
  absl::btree_map<std::string, std::string> defaults_;
};

}  // namespace cuttlefish
