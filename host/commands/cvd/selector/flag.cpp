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
  CF_EXPECT(GetType() != FlagType::kUnknown, "Unsupported flag type");
  auto decode_name = Overload{
      [](auto&& param) -> std::string { return param.Name(); },
  };
  return std::visit(decode_name, flag_);
}

SelectorFlagProxy::FlagType SelectorFlagProxy::GetType() const {
  auto decode_type = Overload{
      [](const SelectorFlag<bool>&) -> FlagType { return FlagType::kBool; },
      [](const SelectorFlag<std::int32_t>&) -> FlagType {
        return FlagType::kInt32;
      },
      [](const SelectorFlag<std::string>&) -> FlagType {
        return FlagType::kString;
      },
      [](auto) -> FlagType { return FlagType::kUnknown; },
  };
  return std::visit(decode_type, flag_);
}

Result<bool> SelectorFlagProxy::HasDefaultValue() const {
  CF_EXPECT(GetType() != FlagType::kUnknown, "Unsupported flag type of typeid");
  auto decode_default_value = Overload{
      [](auto&& flag) -> bool { return flag.HasDefaultValue(); },
  };
  return std::visit(decode_default_value, flag_);
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
