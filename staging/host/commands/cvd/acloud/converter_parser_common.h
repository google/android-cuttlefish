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

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace acloud_impl {

/**
 * Creates acloud-compat value flag with the given list of aliases
 *
 * e.g. {"local-kernel-image", "local-boot-image"} will returns
 * a Flag that accepts "{--local-kernel-image,--local-boot-image} <value>,"
 * and sets the value to std::optional<std::string>.
 *
 */
template <typename T>
static Result<Flag> AcloudCompatFlag(
    const std::vector<std::string>& alias_names, std::optional<T>& opt) {
  CF_EXPECT(!alias_names.empty(), "Alias list must not be empty.");
  Flag new_flag;
  for (const auto& alias_name : alias_names) {
    new_flag.Alias({FlagAliasMode::kFlagConsumesFollowing, "--" + alias_name});
  }
  new_flag.Setter([&opt](const FlagMatch& m) {
    opt = m.value;
    return true;
  });
  return new_flag;
}

}  // namespace acloud_impl
}  // namespace cuttlefish
