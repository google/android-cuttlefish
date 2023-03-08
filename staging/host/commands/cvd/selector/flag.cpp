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

#include "host/commands/cvd/selector/flag.h"

#include "host/commands/cvd/common_utils.h"

namespace cuttlefish {
namespace selector {

Result<std::string> SelectorFlagProxy::Name() const {
  auto decoder = Overload{
      [](const SelectorFlag<bool>& bool_flag) -> Result<std::string> {
        return bool_flag.Name();
      },
      [](const SelectorFlag<std::int32_t>& int32_flag) -> Result<std::string> {
        return int32_flag.Name();
      },
      [](const SelectorFlag<std::string>& string_flag) -> Result<std::string> {
        return string_flag.Name();
      },
      [](auto) -> Result<std::string> {
        return CF_ERR("The type is not handled by SelectorFlagProxy");
      },
  };
  auto name = CF_EXPECT(std::visit(decoder, flag_));
  return name;
}

Result<bool> SelectorFlagProxy::HasDefaultValue() const {
  auto decoder = Overload{
      [](const SelectorFlag<bool>& bool_flag) -> Result<bool> {
        return bool_flag.HasDefaultValue();
      },
      [](const SelectorFlag<std::int32_t>& int32_flag) -> Result<bool> {
        return int32_flag.HasDefaultValue();
      },
      [](const SelectorFlag<std::string>& string_flag) -> Result<bool> {
        return string_flag.HasDefaultValue();
      },
      [](auto) -> Result<bool> {
        return CF_ERR("The type is not handled by SelectorFlagProxy");
      },
  };
  auto has_default_value = CF_EXPECT(std::visit(decoder, flag_));
  return has_default_value;
}

std::vector<SelectorFlagProxy> FlagCollection::Flags() const {
  std::vector<SelectorFlagProxy> flags;
  flags.reserve(name_flag_map_.size());
  for (const auto& [name, flag] : name_flag_map_) {
    flags.push_back(flag);
  }
  return flags;
}

}  // namespace selector
}  // namespace cuttlefish
