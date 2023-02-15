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

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace selector {

/**
 * Data structure to represent user-facing flags
 *
 * Flag in flag_parser.h is more on parsing. gflags library would be
 * slowly depreicated. The SelectorFlag is a specification for
 * a user-facing flag.
 */
template <typename T>
class SelectorFlag {
 public:
  using ValueType = T;

  SelectorFlag(const std::string& name) : name_(name) {}

  SelectorFlag(const std::string& name, const T& default_value)
      : name_(name), default_value_(default_value) {}
  std::string Name() const { return name_; }
  std::string HelpMessage() const { return help_msg_; }
  SelectorFlag& SetHelpMessage(const std::string& help_msg) & {
    help_msg_ = help_msg;
    return *this;
  }
  SelectorFlag SetHelpMessage(const std::string& help_msg) && {
    help_msg_ = help_msg;
    return *this;
  }
  bool HasDefaultValue() const { return default_value_ != std::nullopt; }

  // returns CF_ERR if parsing error,
  // returns std::nullopt if parsing was okay but the flag wasn't given
  Result<std::optional<T>> FilterFlag(cvd_common::Args& args) {
    const int args_initial_size = args.size();
    if (args_initial_size == 0) {
      return std::nullopt;
    }
    T value;
    CF_EXPECT(ParseFlags({GflagsCompatFlag(name_, value)}, args),
              "Failed to parse --" << name_);
    if (args.size() == args_initial_size) {
      // not consumed
      return std::nullopt;
    }
    return value;
  }

  // Parses the arguments. If flag is given, returns the parsed value. If not,
  // returns the default value if any. If no default value, it returns CF_ERR.
  Result<T> ParseFlag(cvd_common::Args& args) {
    auto value_opt = CF_EXPECT(FilterFlag(args));
    if (!value_opt) {
      CF_EXPECT(default_value_ != std::nullopt);
      value_opt = default_value_;
    }
    return *value_opt;
  }

  const std::string name_;
  std::string help_msg_;
  std::optional<T> default_value_;
};

class FlagCollection {
 public:
  using FlagType = std::variant<SelectorFlag<std::int32_t>, SelectorFlag<bool>,
                                SelectorFlag<std::string>>;

  template <typename T>
  Result<void> EnrollFlag(SelectorFlag<T>&& flag) {
    CF_EXPECT(!Contains(name_flag_map_, flag.Name()),
              flag.Name() << " is already registered.");
    name_flag_map_.emplace(flag.Name(), std::move(flag));
    return {};
  }

  template <typename T>
  Result<SelectorFlag<T>> GetFlag(const std::string& name) const {
    const auto itr = name_flag_map_.find(name);
    CF_EXPECT(itr != name_flag_map_.end(),
              "Flag \"" << name << "\" is not found.");
    const FlagType& flag_var = itr->second;
    const auto* flag_ptr = std::get_if<SelectorFlag<T>>(&flag_var);
    CF_EXPECT(flag_ptr != nullptr,
              "The type of the requested flag \"" << name << "\" is wrong.");
    return *flag_ptr;
  }

 private:
  std::unordered_map<std::string, FlagType> name_flag_map_;
};

}  // namespace selector
}  // namespace cuttlefish
