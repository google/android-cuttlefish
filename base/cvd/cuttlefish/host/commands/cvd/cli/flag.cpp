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

#include "host/commands/cvd/cli/flag.h"

#include "host/commands/cvd/utils/common.h"

namespace cuttlefish {

Result<std::string> CvdFlagProxy::Name() const {
  auto decode_name = Overload{
      [](auto&& param) -> std::string { return param.Name(); },
  };
  return std::visit(decode_name, flag_);
}

Result<bool> CvdFlagProxy::HasDefaultValue() const {
  auto decode_default_value = Overload{
      [](auto&& flag) -> bool { return flag.HasDefaultValue(); },
  };
  return std::visit(decode_default_value, flag_);
}

std::vector<CvdFlagProxy> FlagCollection::Flags() const {
  std::vector<CvdFlagProxy> flags;
  flags.reserve(name_flag_map_.size());
  for (const auto& [name, flag] : name_flag_map_) {
    flags.push_back(flag);
  }
  return flags;
}

template <typename T>
static Result<std::optional<CvdFlagProxy::ValueVariant>> FilterKnownTypeFlag(
    const CvdFlag<T>& flag, cvd_common::Args& args) {
  std::optional<T> opt = CF_EXPECT(flag.FilterFlag(args));
  if (!opt) {
    return std::nullopt;
  }
  CvdFlagProxy::ValueVariant value_variant = *opt;
  return value_variant;
}

Result<std::optional<CvdFlagProxy::ValueVariant>> CvdFlagProxy::FilterFlag(
    cvd_common::Args& args) const {
  std::optional<CvdFlagProxy::ValueVariant> output;
  auto filter_flag = Overload{
      [&args](const CvdFlag<std::int32_t>& int32_t_flag)
          -> Result<std::optional<ValueVariant>> {
        return FilterKnownTypeFlag(int32_t_flag, args);
      },
      [&args](const CvdFlag<bool>& bool_flag)
          -> Result<std::optional<ValueVariant>> {
        return FilterKnownTypeFlag(bool_flag, args);
      },
      [&args](const CvdFlag<std::string>& string_flag)
          -> Result<std::optional<ValueVariant>> {
        return FilterKnownTypeFlag(string_flag, args);
      },
      [](auto) -> Result<std::optional<ValueVariant>> {
        return CF_ERR("Invalid type is passed to FlagCollection::FilterFlags");
      },
  };
  output = CF_EXPECT(std::visit(filter_flag, flag_));
  return output;
}

Result<std::unordered_map<std::string, FlagCollection::FlagValuePair>>
FlagCollection::FilterFlags(cvd_common::Args& args) const {
  std::unordered_map<std::string, FlagCollection::FlagValuePair> output;
  for (const auto& [name, flag_proxy] : name_flag_map_) {
    auto value_opt = CF_EXPECT(flag_proxy.FilterFlag(args));
    if (!value_opt) {
      continue;
    }
    output.emplace(name,
                   FlagValuePair{.flag = flag_proxy, .value = *value_opt});
  }
  return output;
}

}  // namespace cuttlefish
