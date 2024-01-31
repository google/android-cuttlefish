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

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cuttlefish {

class FlagInfo {
 public:
  using FlagInfoFieldMap = std::unordered_map<std::string, std::string>;
  static std::unique_ptr<FlagInfo> Create(
      const FlagInfoFieldMap& field_value_map);
  const std::string& Name() const { return name_; }
  const std::string& Type() const { return type_; }

 private:
  // field_value_map must have needed fields; guaranteed by the factory
  // function, static Create().
  FlagInfo(const FlagInfoFieldMap& field_value_map)
      : name_(field_value_map.at("name")), type_(field_value_map.at("type")) {}

  // TODO(kwstephenkim): add more fields
  std::string name_;
  std::string type_;
};

using FlagInfoPtr = std::unique_ptr<FlagInfo>;

std::optional<std::vector<FlagInfoPtr>> CollectFlagsFromHelpxml(
    const std::string& xml_str);

}  // namespace cuttlefish
