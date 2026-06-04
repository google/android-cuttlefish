/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "cuttlefish/flag_parser/flag.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

struct Separated {
  std::vector<std::string> args_before_mark;
  std::vector<std::string> args_after_mark;
};
Separated SeparateByEndOfOptionMark(std::vector<std::string> args) {
  std::vector<std::string> args_before_mark;
  std::vector<std::string> args_after_mark;

  auto itr = std::find(args.begin(), args.end(), "--");
  bool has_mark = (itr != args.end());
  if (!has_mark) {
    args_before_mark = std::move(args);
  } else {
    args_before_mark.insert(args_before_mark.end(), args.begin(), itr);
    args_after_mark.insert(args_after_mark.end(), itr + 1, args.end());
  }

  return Separated{
      .args_before_mark = std::move(args_before_mark),
      .args_after_mark = std::move(args_after_mark),
  };
}

Result<void> ConsumeFlagsImpl(const std::vector<Flag>& flags,
                                     std::vector<std::string>& args) {
  for (const auto& flag : flags) {
    CF_EXPECT(flag.Parse(args));
  }
  return {};
}

Result<void> ConsumeFlagsImpl(const std::vector<Flag>& flags,
                                     std::vector<std::string>&& args) {
  for (const auto& flag : flags) {
    CF_EXPECT(flag.Parse(args));
  }
  return {};
}

}  // namespace

std::ostream& operator<<(std::ostream& out, const FlagAlias& alias) {
  switch (alias.mode) {
    case FlagAliasMode::kFlagExact:
      return out << alias.name;
    case FlagAliasMode::kFlagPrefix:
      return out << alias.name << "*";
    case FlagAliasMode::kFlagConsumesFollowing:
      return out << alias.name << " *";
    default:
      LOG(FATAL) << "Unexpected flag alias mode " << (int)alias.mode;
  }
  return out;
}

Flag& Flag::UnvalidatedAlias(const FlagAlias& alias) & {
  aliases_.push_back(alias);
  return *this;
}
Flag Flag::UnvalidatedAlias(const FlagAlias& alias) && {
  aliases_.push_back(alias);
  return *this;
}

void Flag::ValidateAlias(const FlagAlias& alias) {
  using absl::StartsWith;

  CHECK(StartsWith(alias.name, "-")) << "Flags should start with \"-\"";
  if (alias.mode == FlagAliasMode::kFlagPrefix) {
    CHECK(absl::EndsWith(alias.name, "=")) << "Prefix flags must end with '='";
  }

  CHECK(!HasAlias(alias)) << "Duplicate flag alias: " << alias.name;
  if (alias.mode == FlagAliasMode::kFlagConsumesFollowing) {
    CHECK(!HasAlias({FlagAliasMode::kFlagExact, alias.name}))
        << "Overlapping flag aliases for " << alias.name;
  } else if (alias.mode == FlagAliasMode::kFlagExact) {
    CHECK(!HasAlias({FlagAliasMode::kFlagConsumesFollowing, alias.name}))
        << "Overlapping flag aliases for " << alias.name;
  }
}

Flag& Flag::Alias(const FlagAlias& alias) & {
  ValidateAlias(alias);
  aliases_.push_back(alias);
  return *this;
}
Flag Flag::Alias(const FlagAlias& alias) && {
  ValidateAlias(alias);
  aliases_.push_back(alias);
  return *this;
}

Flag& Flag::Help(const std::string& help) & {
  help_ = help;
  return *this;
}
Flag Flag::Help(const std::string& help) && {
  help_ = help;
  return *this;
}

Flag& Flag::Getter(std::function<std::string()> fn) & {
  getter_ = std::move(fn);
  return *this;
}
Flag Flag::Getter(std::function<std::string()> fn) && {
  getter_ = std::move(fn);
  return *this;
}

Flag& Flag::Setter(std::function<Result<void>(const FlagMatch&)> setter) & {
  setter_ = std::move(setter);
  return *this;
}
Flag Flag::Setter(std::function<Result<void>(const FlagMatch&)> setter) && {
  setter_ = std::move(setter);
  return *this;
}

Flag& Flag::AddValidator(std::function<Result<void>()> validator) & {
  validators_.emplace_back(std::move(validator));
  return *this;
}
Flag Flag::AddValidator(std::function<Result<void>()> validator) && {
  validators_.emplace_back(std::move(validator));
  return *this;
}

Result<Flag::FlagProcessResult> Flag::Process(
    const std::string& arg, const std::optional<std::string>& next_arg) const {
  auto normalized_arg = absl::StrReplaceAll(arg, {{"-", "_"}});
  if (!setter_ && !aliases_.empty()) {
    return CF_ERRF("No setter for flag with alias {}", aliases_[0].name);
  }
  for (auto& alias : aliases_) {
    auto normalized_alias = absl::StrReplaceAll(alias.name, {{"-", "_"}});
    switch (alias.mode) {
      case FlagAliasMode::kFlagConsumesFollowing:
        if (normalized_arg != normalized_alias) {
          continue;
        }
        CF_EXPECTF(next_arg.has_value(), "Expected an argument after \"{}\"",
                   arg);
        CF_EXPECTF(SetAndValidate({arg, *next_arg}),
                   "Processing \"{}\" \"{}\" failed", arg, *next_arg);
        return FlagProcessResult::kFlagConsumedWithFollowing;
      case FlagAliasMode::kFlagExact:
        if (normalized_arg != normalized_alias) {
          continue;
        }
        CF_EXPECTF(SetAndValidate({arg, arg}), "Processing \"{}\" failed", arg);
        return FlagProcessResult::kFlagConsumed;
      case FlagAliasMode::kFlagPrefix:
        if (!absl::StartsWith(normalized_arg, normalized_alias)) {
          continue;
        }
        CF_EXPECTF(SetAndValidate({alias.name, arg.substr(alias.name.size())}),
                   "Processing \"{}\" failed", arg);
        return FlagProcessResult::kFlagConsumed;
      default:
        return CF_ERRF("Unknown flag alias mode: {}", (int)alias.mode);
    }
  }
  return FlagProcessResult::kFlagSkip;
}

Result<void> Flag::SetAndValidate(const FlagMatch& match) const {
  CF_EXPECT((*setter_)(match));
  for (auto& validator: validators_) {
    CF_EXPECT(validator());
  }
  return {};
}

Result<void> Flag::Parse(std::vector<std::string>& arguments) const {
  for (int i = 0; i < arguments.size();) {
    std::string arg = arguments[i];
    std::optional<std::string> next_arg;
    if (i < arguments.size() - 1) {
      next_arg = arguments[i + 1];
    }
    switch (CF_EXPECT(Process(arg, next_arg))) {
      case FlagProcessResult::kFlagConsumed:
        arguments.erase(arguments.begin() + i);
        break;
      case FlagProcessResult::kFlagConsumedWithFollowing:
        arguments.erase(arguments.begin() + i, arguments.begin() + i + 2);
        break;
      case FlagProcessResult::kFlagSkip:
        i++;
        break;
    }
  }
  return {};
}
Result<void> Flag::Parse(std::vector<std::string>&& arguments) const {
  CF_EXPECT(Parse(static_cast<std::vector<std::string>&>(arguments)));
  return {};
}

bool Flag::HasAlias(const FlagAlias& test) const {
  for (const auto& alias : aliases_) {
    if (alias.mode == test.mode && alias.name == test.name) {
      return true;
    }
  }
  return false;
}

std::ostream& operator<<(std::ostream& out, const Flag& flag) {
  for (auto it = flag.aliases_.begin(); it != flag.aliases_.end(); it++) {
    if (it != flag.aliases_.begin()) {
      out << ", ";
    }
    out << *it;
  }
  out << "\n";
  if (flag.help_) {
    out <<  *flag.help_ << "\n";
  }
  if (flag.getter_) {
    out << "Current value: \"" << (*flag.getter_)() << "\"\n";
  }
  return out;
}

Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>& args,
                          const bool recognize_end_of_option_mark) {
  if (!recognize_end_of_option_mark) {
    CF_EXPECT(ConsumeFlagsImpl(flags, args));
    return {};
  }
  auto separated = SeparateByEndOfOptionMark(std::move(args));
  args.clear();
  auto result = ConsumeFlagsImpl(flags, separated.args_before_mark);
  args = std::move(separated.args_before_mark);
  args.insert(args.end(),
              std::make_move_iterator(separated.args_after_mark.begin()),
              std::make_move_iterator(separated.args_after_mark.end()));
  CF_EXPECT(std::move(result));
  return {};
}

Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>&& args,
                          const bool recognize_end_of_option_mark) {
  if (!recognize_end_of_option_mark) {
    CF_EXPECT(ConsumeFlagsImpl(flags, std::move(args)));
    return {};
  }
  auto separated = SeparateByEndOfOptionMark(std::move(args));
  CF_EXPECT(ConsumeFlagsImpl(flags, std::move(separated.args_before_mark)));
  return {};
}

Result<void> ConsumeFlagsConstrained(const std::vector<Flag>& flags,
                                     std::vector<std::string>& args) {
  while (!args.empty()) {
    const std::string& first_arg = args[0];
    std::optional<std::string> next_arg;
    if (args.size() > 1) {
      next_arg = args[1];
    }
    Flag::FlagProcessResult outcome = Flag::FlagProcessResult::kFlagSkip;
    for (const Flag& flag : flags) {
      Flag::FlagProcessResult flag_outcome = 
          CF_EXPECT(flag.Process(first_arg, next_arg));
      if (flag_outcome == Flag::FlagProcessResult::kFlagSkip) {
        continue;
      }
      CF_EXPECTF(outcome == Flag::FlagProcessResult::kFlagSkip,
                 "Multiple '{}' handlers", first_arg);
      outcome = flag_outcome;
    }
    switch (outcome) {
      case Flag::FlagProcessResult::kFlagSkip:
        return {};
      case Flag::FlagProcessResult::kFlagConsumed:
        args.erase(args.begin());
        break;
      case Flag::FlagProcessResult::kFlagConsumedWithFollowing:
        args.erase(args.begin(), args.begin() + 2);
        break;
    }
  }
  return {};
}

Result<void> ConsumeFlagsConstrained(const std::vector<Flag>& flags,
                                     std::vector<std::string>&& args) {
  std::vector<std::string>& args_ref = args;
  CF_EXPECT(ConsumeFlagsConstrained(flags, args_ref));
  return {};
}

Flag InvalidFlagGuard() {
  return Flag("_invalid_flag_guard_")
      .UnvalidatedAlias({FlagAliasMode::kFlagPrefix, "-"})
      .Help(
          "This executable only supports the flags in `-help`. Positional "
          "arguments may be supported.")
      .Setter([](const FlagMatch& match) -> Result<void> {
        return CF_ERRF("Unknown flag \"{}\"", match.value);
      });
}

Flag UnexpectedArgumentGuard() {
  return Flag("_unexpected_argument_guard_")
      .UnvalidatedAlias({FlagAliasMode::kFlagPrefix, ""})
      .Help(
          "This executable only supports the flags in `-help`. Positional "
          "arguments are not supported.")
      .Setter([](const FlagMatch& match) -> Result<void> {
        return CF_ERRF("Unexpected argument \"{}\"", match.value);
      });
}

}  // namespace cuttlefish
