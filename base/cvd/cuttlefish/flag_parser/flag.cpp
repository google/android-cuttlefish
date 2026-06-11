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
#include <cstddef>
#include <functional>
#include <iostream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

// Removes "--" or "-" from the beginning. Returns true if it removed something.
bool ConsumeDashes(std::string_view& flag_arg) {
  if (!absl::ConsumePrefix(&flag_arg, "-")) {
    return false;
  }
  absl::ConsumePrefix(&flag_arg, "-");
  return true;
}

bool MatchNormalized(std::string_view str1, std::string_view str2) {
  const std::string n1 = absl::StrReplaceAll(str1, {{"-", "_"}});
  const std::string n2 = absl::StrReplaceAll(str2, {{"-", "_"}});
  return n1 == n2;
}

std::string DefaultValueNameHint(std::string_view name) {
  std::vector<std::string_view> parts =
      absl::StrSplit(name, absl::ByAnyChar("-_"), absl::SkipEmpty());
  if (parts.empty()) {
    return "VAL";
  }
  return absl::AsciiStrToUpper(parts.back());
}

}  // namespace

Flag Flag::StringFlag(std::string name) {
  return Flag(std::move(name), Style::String);
}

Flag Flag::BoolFlag(std::string name) {
  return Flag(std::move(name), Style::Bool);
}

Flag& Flag::Alias(std::string alias) & {
  ValidateAlias(alias);
  aliases_.emplace_back(std::move(alias));
  return *this;
}
Flag Flag::Alias(std::string alias) && { return std::move(Alias(alias)); }

Flag& Flag::Help(std::string help) & {
  help_ = std::move(help);
  return *this;
}
Flag Flag::Help(std::string help) && { return std::move(Help(help)); }

Flag& Flag::ValueNameHint(std::string value_name_hint) & {
  value_name_hint_ = std::move(value_name_hint);
  return *this;
}
Flag Flag::ValueNameHint(std::string value_name_hint) && {
  return std::move(ValueNameHint(value_name_hint));
}

Flag& Flag::Getter(std::function<std::string()> getter) & {
  getter_ = std::move(getter);
  return *this;
}
Flag Flag::Getter(std::function<std::string()> getter) && {
  return std::move(Getter(getter));
}

Flag& Flag::Setter(std::function<Result<void>(std::string_view)> setter) & {
  setter_ = std::move(setter);
  return *this;
}
Flag Flag::Setter(std::function<Result<void>(std::string_view)> setter) && {
  return std::move(Setter(setter));
}

Flag& Flag::AddValidator(std::function<Result<void>()> validator) & {
  validators_.emplace_back(std::move(validator));
  return *this;
}
Flag Flag::AddValidator(std::function<Result<void>()> validator) && {
  return std::move(AddValidator(validator));
}

std::string Flag::Name() const { return aliases_.front(); }

std::string Flag::Synopsis() const {
  std::vector<std::string> options;
  for (const std::string& alias : aliases_) {
    switch (style_) {
      case Flag::Style::String:
        options.emplace_back(absl::StrCat("--", alias, "=", value_name_hint_));
        break;
      case Flag::Style::Bool:
        options.emplace_back("--[no]" + alias);
        break;
    }
  }
  return absl::StrJoin(options, ", ");
}

const std::string& Flag::Description() const { return help_; }

std::string Flag::CurrentValue() const { return getter_(); }

const std::string& Flag::ValueNameHint() const { return value_name_hint_; }

Flag::Flag(std::string name, Flag::Style style) : style_(style) {
  ValidateAlias(name);
  value_name_hint_ = DefaultValueNameHint(name);
  aliases_.emplace_back(std::move(name));
}

void Flag::ValidateAlias(const std::string& alias) {
  CHECK(!alias.starts_with("-")) << "Flag aliases should not start with \"-\"";
  CHECK(!Contains(aliases_, alias)) << "Duplicate flag alias: " << alias;
}

Result<size_t> Flag::Match(std::string_view current_arg,
                           std::span<const std::string> following_args) const {
  switch (style_) {
    case Style::String:
      return CF_EXPECT(MatchStringStyleFlag(current_arg, following_args));
    case Style::Bool:
      return CF_EXPECT(MatchBoolStyleFlag(current_arg));
  }
}

Result<size_t> Flag::MatchStringStyleFlag(
    std::string_view current_arg,
    std::span<const std::string> following_args) const {
  if (!ConsumeDashes(current_arg)) {
    // Doesn't begin with "-"
    return 0;
  }
  for (const std::string& alias : aliases_) {
    if (MatchNormalized(current_arg.substr(0, alias.size() + 1), alias + "=")) {
      CF_EXPECT(SetAndValidate(current_arg.substr(alias.size() + 1)));
      return 1;
    }
  }
  std::vector<std::string> keys;
  for (const std::string& alias : aliases_) {
    if (!MatchNormalized(current_arg, alias)) {
      continue;
    }
    CF_EXPECTF(!following_args.empty(), "No argument provided for flag '{}'",
               current_arg);
    CF_EXPECT(SetAndValidate(following_args[0]));
    return 2;
  }
  return 0;
}

Result<size_t> Flag::MatchBoolStyleFlag(std::string_view current_arg) const {
  if (!ConsumeDashes(current_arg)) {
    // Doesn't begin with "-"
    return 0;
  }
  for (const std::string& alias : aliases_) {
    if (MatchNormalized(current_arg.substr(0, alias.size() + 1), alias + "=")) {
      CF_EXPECT(SetAndValidate(current_arg.substr(alias.size() + 1)));
      return 1;
    }
    if (MatchNormalized(current_arg, alias)) {
      CF_EXPECT(SetAndValidate("yes"));
      return 1;
    }
    if (MatchNormalized(current_arg, "no" + alias)) {
      CF_EXPECT(SetAndValidate("no"));
      return 1;
    }
  }
  return 0;
}

Result<void> Flag::SetAndValidate(std::string_view value) const {
  CF_EXPECT(setter_(value));
  for (auto& validator : validators_) {
    CF_EXPECT(validator());
  }
  return {};
}

std::ostream& operator<<(std::ostream& out, const Flag& flag) {
  out << flag.Synopsis() << "\n";

  std::string help = flag.Description();
  if (!help.empty()) {
    out << help << "\n";
  }
  out << "Current value: \"" << flag.CurrentValue() << "\"\n";
  return out;
}

Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>& args,
                          ConsumeFlagsOpts opts) {
  std::unordered_set<std::string_view> aliases;
  for (const Flag& flag : flags) {
    for (std::string_view alias : flag.aliases_) {
      const auto [_, inserted] = aliases.insert(alias);
      CF_EXPECTF(!!inserted,
                 "Ambiguous argument parsing possible: '--{}' could be matched "
                 "by multiple flags",
                 alias);
    }
  }

  auto end = args.end();
  if (opts.stop_at_double_dashes) {
    end = std::find(args.begin(), args.end(), "--");
  }

  auto available_slot_it = args.begin();
  auto match_it = args.begin();
  absl::Cleanup delete_args = [&args, &available_slot_it, &match_it, opts]() {
    auto after_erase_it = args.erase(available_slot_it, match_it);
    // TODO(jemoreira): Removing the "--" separator from the arguments list
    // prevents distinguishing between unconsumed arguments before and after the
    // separator, but existing usage expects it to be removed. The separator
    // should be left in place and its index returned so the callers have as
    // much flexibility as possible.
    if (opts.stop_at_double_dashes && after_erase_it != args.end() &&
        *after_erase_it == "--") {
      args.erase(after_erase_it);
    }
  };
  while (match_it != end) {
    size_t consumed = 0;
    for (const Flag& flag : flags) {
      consumed = CF_EXPECT(flag.Match(*match_it, std::span(match_it + 1, end)));
      if (consumed) {
        break;
      }
    }
    if (consumed) {
      match_it += consumed;
      continue;
    }
    CF_EXPECTF(!opts.fail_on_unexpected_argument, "Unexpected argument \"{}\"",
               *match_it);
    // This argument was not consumed, keep it at the first available consumed
    // slot
    if (available_slot_it != match_it) {
      *available_slot_it = std::move(*match_it);
    }
    ++available_slot_it;
    ++match_it;
  }
  return {};
}

Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>&& args,
                          ConsumeFlagsOpts opts) {
  CF_EXPECT(ConsumeFlags(flags, args, opts));
  return {};
}

}  // namespace cuttlefish
