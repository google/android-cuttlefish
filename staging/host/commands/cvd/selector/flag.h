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
  Result<T> DefaultValue() const {
    CF_EXPECT(HasDefaultValue());
    return *default_value_;
  }

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

 private:
  const std::string name_;
  std::string help_msg_;
  std::optional<T> default_value_;
};

class SelectorFlagProxy {
  friend class FlagCollection;

 public:
  template <typename T>
  SelectorFlagProxy(SelectorFlag<T>&& flag) : flag_{std::move(flag)} {}

  template <typename T>
  const SelectorFlag<T>* GetFlag() const {
    return std::get_if<SelectorFlag<T>>(&flag_);
  }

  template <typename T>
  SelectorFlag<T>* GetFlag() {
    return std::get_if<SelectorFlag<T>>(&flag_);
  }

  /*
   * If the actual type of flag_ is not handled by SelectorFlagProxy, it is a
   * developer error, and the Name() and HasDefaultValue() will returns
   * CF_ERR
   */
  Result<std::string> Name() const;
  Result<bool> HasDefaultValue() const;

  template <typename T>
  Result<T> DefaultValue() const {
    const bool has_default_value = CF_EXPECT(HasDefaultValue());
    CF_EXPECT(has_default_value == true);
    const auto* ptr = CF_EXPECT(std::get_if<SelectorFlag<T>>(&flag_));
    CF_EXPECT(ptr != nullptr);
    return ptr->DefaultValue();
  }

  // returns CF_ERR if parsing error,
  // returns std::nullopt if parsing was okay but the flag wasn't given
  template <typename T>
  Result<void> FilterFlag(cvd_common::Args& args, std::optional<T>& output) {
    output = std::nullopt;
    auto* ptr = CF_EXPECT(std::get_if<SelectorFlag<T>>(&flag_));
    CF_EXPECT(ptr != nullptr);
    output = CF_EXPECT(ptr->FilterFlag(args));
    return {};
  }

  // Parses the arguments. If flag is given, returns the parsed value. If not,
  // returns the default value if any. If no default value, it returns CF_ERR.
  template <typename T>
  Result<void> ParseFlag(cvd_common::Args& args, T& output) {
    bool has_default_value = CF_EXPECT(HasDefaultValue());
    CF_EXPECT(has_default_value == true);
    auto* ptr = CF_EXPECT(std::get_if<SelectorFlag<T>>(&flag_));
    CF_EXPECT(ptr != nullptr);
    output = CF_EXPECT(ptr->ParseFlag(args));
    return {};
  }

 private:
  std::variant<SelectorFlag<std::int32_t>, SelectorFlag<bool>,
               SelectorFlag<std::string>>
      flag_;
};

class FlagCollection {
 public:
  template <typename T>
  Result<void> EnrollFlag(SelectorFlag<T>&& flag) {
    CF_EXPECT(!Contains(name_flag_map_, flag.Name()),
              flag.Name() << " is already registered.");
    name_flag_map_.emplace(flag.Name(), SelectorFlagProxy(std::move(flag)));
    return {};
  }

  Result<SelectorFlagProxy> GetFlag(const std::string& name) const {
    const auto itr = name_flag_map_.find(name);
    CF_EXPECT(itr != name_flag_map_.end(),
              "Flag \"" << name << "\" is not found.");
    const SelectorFlagProxy& flag_proxy = itr->second;
    return flag_proxy;
  }

  std::vector<SelectorFlagProxy> Flags() const;

 private:
  std::unordered_map<std::string, SelectorFlagProxy> name_flag_map_;
};

}  // namespace selector
}  // namespace cuttlefish
