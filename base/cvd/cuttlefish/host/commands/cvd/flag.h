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

#include <cstdint>
#include <functional>
#include <memory>
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

/**
 * Data structure to represent cvd user-facing flags
 *
 * Flag in flag_parser.h is more on parsing. gflags library would be
 * slowly depreicated. The cvd driver and selector flags are a specification for
 * a user-facing flag.
 */
template <typename T>
class CvdFlag {
 public:
  using GflagFactoryCallback =
      std::function<Flag(const std::string& name, T& value_out)>;
  CvdFlag(const std::string& name)
      : name_(name),
        gflag_factory_cb([](const std::string& name, T& value_out) {
          return GflagsCompatFlag(name, value_out);
        }) {}

  CvdFlag(const std::string& name, const T& default_value)
      : name_(name),
        default_value_(default_value),
        gflag_factory_cb([](const std::string& name, T& value_out) {
          return GflagsCompatFlag(name, value_out);
        }) {}

  std::string Name() const { return name_; }
  std::string HelpMessage() const { return help_msg_; }
  CvdFlag& SetHelpMessage(const std::string& help_msg) & {
    help_msg_ = help_msg;
    return *this;
  }
  CvdFlag SetHelpMessage(const std::string& help_msg) && {
    help_msg_ = help_msg;
    return *this;
  }
  bool HasDefaultValue() const { return default_value_ != std::nullopt; }
  Result<T> DefaultValue() const {
    CF_EXPECT(HasDefaultValue());
    return *default_value_;
  }

  CvdFlag& SetGflagFactory(GflagFactoryCallback factory) & {
    gflag_factory_cb = std::move(factory);
    return *this;
  }
  CvdFlag SetGflagFactory(GflagFactoryCallback factory) && {
    gflag_factory_cb = std::move(factory);
    return *this;
  }

  // returns CF_ERR if parsing error,
  // returns std::nullopt if parsing was okay but the flag wasn't given
  Result<std::optional<T>> FilterFlag(cvd_common::Args& args) const {
    const int args_initial_size = args.size();
    if (args_initial_size == 0) {
      return std::nullopt;
    }
    T value;
    CF_EXPECT(ParseFlags({gflag_factory_cb(name_, value)}, args),
              "Failed to parse --" << name_);
    if (args.size() == args_initial_size) {
      // not consumed
      return std::nullopt;
    }
    return value;
  }

  // Parses the arguments. If flag is given, returns the parsed value. If not,
  // returns the default value if any. If no default value, it returns CF_ERR.
  Result<T> CalculateFlag(cvd_common::Args& args) const {
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
  /**
   * A callback function to generate Flag defined in
   * common/libs/utils/flag_parser.h. The name is this CvdFlag's name.
   * The value is a buffer that is kept in this object
   */
  GflagFactoryCallback gflag_factory_cb;
};

class CvdFlagProxy {
  friend class FlagCollection;

 public:
  enum class FlagType : std::uint32_t {
    kUnknown = 0,
    kBool,
    kInt32,
    kString,
  };
  template <typename T>
  CvdFlagProxy(CvdFlag<T>&& flag) : flag_{std::move(flag)} {}

  template <typename T>
  const CvdFlag<T>* GetFlag() const {
    return std::get_if<CvdFlag<T>>(&flag_);
  }

  template <typename T>
  CvdFlag<T>* GetFlag() {
    return std::get_if<CvdFlag<T>>(&flag_);
  }

  /*
   * If the actual type of flag_ is not handled by SelectorFlagProxy, it is a
   * developer error, and the Name() and HasDefaultValue() will returns
   * CF_ERR
   */
  Result<std::string> Name() const;
  Result<bool> HasDefaultValue() const;

  FlagType GetType() const;

  template <typename T>
  Result<T> DefaultValue() const {
    const bool has_default_value = CF_EXPECT(HasDefaultValue());
    CF_EXPECT(has_default_value == true);
    const auto* ptr = CF_EXPECT(std::get_if<CvdFlag<T>>(&flag_));
    CF_EXPECT(ptr != nullptr);
    return ptr->DefaultValue();
  }

  // returns CF_ERR if parsing error,
  // returns std::nullopt if parsing was okay but the flag wasn't given
  template <typename T>
  Result<std::optional<T>> FilterFlag(cvd_common::Args& args) const {
    std::optional<T> output;
    const auto* ptr = CF_EXPECT(std::get_if<CvdFlag<T>>(&flag_));
    CF_EXPECT(ptr != nullptr);
    output = CF_EXPECT(ptr->FilterFlag(args));
    return output;
  }

  // Parses the arguments. If flag is given, returns the parsed value. If not,
  // returns the default value if any. If no default value, it returns CF_ERR.
  template <typename T>
  Result<T> CalculateFlag(cvd_common::Args& args) const {
    bool has_default_value = CF_EXPECT(HasDefaultValue());
    CF_EXPECT(has_default_value == true);
    const auto* ptr = CF_EXPECT(std::get_if<CvdFlag<T>>(&flag_));
    CF_EXPECT(ptr != nullptr);
    T output = CF_EXPECT(ptr->CalculateFlag(args));
    return output;
  }

  using ValueVariant = std::variant<std::int32_t, bool, std::string>;

  // Returns std::nullopt when the parsing goes okay but the flag wasn't given
  // Returns ValueVariant when the flag was given in args
  // Returns CF_ERR when the parsing failed or the type is not supported
  Result<std::optional<ValueVariant>> FilterFlag(cvd_common::Args& args) const;

 private:
  std::variant<CvdFlag<std::int32_t>, CvdFlag<bool>, CvdFlag<std::string>>
      flag_;
};

class FlagCollection {
 public:
  using ValueVariant = CvdFlagProxy::ValueVariant;

  Result<void> EnrollFlag(CvdFlagProxy&& flag) {
    auto name = CF_EXPECT(flag.Name());
    CF_EXPECT(!Contains(name_flag_map_, name),
              name << " is already registered.");
    name_flag_map_.emplace(name, std::move(flag));
    return {};
  }

  template <typename T>
  Result<void> EnrollFlag(CvdFlag<T>&& flag) {
    CF_EXPECT(EnrollFlag(CvdFlagProxy(std::move(flag))));
    return {};
  }

  Result<CvdFlagProxy> GetFlag(const std::string& name) const {
    const auto itr = name_flag_map_.find(name);
    CF_EXPECT(itr != name_flag_map_.end(),
              "Flag \"" << name << "\" is not found.");
    const CvdFlagProxy& flag_proxy = itr->second;
    return flag_proxy;
  }

  std::vector<CvdFlagProxy> Flags() const;

  struct FlagValuePair {
    std::optional<ValueVariant> value_opt;
    CvdFlagProxy flag;
  };

  // do not consider default values
  Result<std::unordered_map<std::string, FlagValuePair>> FilterFlags(
      cvd_common::Args& args) const;
  // consider default values
  Result<std::unordered_map<std::string, FlagValuePair>> CalculateFlags(
      cvd_common::Args& args) const;

 private:
  std::unordered_map<std::string, CvdFlagProxy> name_flag_map_;
};

}  // namespace cuttlefish
