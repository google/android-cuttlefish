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

#include <optional>
#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"

namespace cuttlefish {
namespace selector {

struct SelectorOptions {
  std::optional<std::string> group_name;
  std::optional<std::vector<std::string>> instance_names;

  bool HasOptions() const {
    return group_name.has_value() || instance_names.has_value();
  }

  bool operator==(const SelectorOptions&) const = default;
};

std::vector<Flag> BuildCommonSelectorFlags(SelectorOptions& opts);

}  // namespace selector
}  // namespace cuttlefish
