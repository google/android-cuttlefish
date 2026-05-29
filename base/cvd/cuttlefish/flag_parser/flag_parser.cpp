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

#include "cuttlefish/flag_parser/flag_parser.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <concepts>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/strings/str_replace.h"
#include "absl/strings/str_join.h"
#include <fmt/format.h>
#include <fmt/ranges.h>  // NOLINT(misc-include-cleaner): version difference
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

/*
 * From external/gflags/src, commit:
 *  061f68cd158fa658ec0b9b2b989ed55764870047
 *
 */
constexpr std::array help_bool_opts{
    "help", "helpfull", "helpshort", "helppackage", "helpxml", "version", "h"};
constexpr std::array help_str_opts{
    "helpon",
    "helpmatch",
};

bool ShouldBeNullOpt(std::string_view value, CoerceToNullopt opt) {
  switch (opt) {
    case CoerceToNullopt::None:
      return false;
    case CoerceToNullopt::UnsetKeyword:
      return value == "unset";
    case CoerceToNullopt::EmptyString:
      return value.empty();
  };
}

std::string NullOptValue(CoerceToNullopt opt) {
  switch (opt) {
    case CoerceToNullopt::None:
      return "[not set]";
    case CoerceToNullopt::UnsetKeyword:
      return "unset";
    case CoerceToNullopt::EmptyString:
      return "";
  }
}

template <typename T>
std::string FlagValueToString(const T& value) {
  return fmt::format("{}", value);
}

template <typename T>
Result<T> ParseFlagValue(std::string_view str);

// Setting and getting strings
template <>
std::string FlagValueToString<std::string>(const std::string& value) {
  return fmt::format("{}", value);
}
template <>
Result<std::string> ParseFlagValue<std::string>(std::string_view str) {
  return std::string(str);
}

// Setter for unsigned and size_t, default getter works
template<>
Result<size_t> ParseFlagValue<size_t>(std::string_view str) {
  size_t value;
  CF_EXPECT(absl::SimpleAtoi(str, &value));
  return value;
}
template<>
Result<unsigned> ParseFlagValue<unsigned>(std::string_view str) {
  unsigned value;
  CF_EXPECT(absl::SimpleAtoi(str, &value));
  return value;
}

// Setter for bool, default getter is fine
template<>
Result<bool> ParseFlagValue<bool>(std::string_view str) {
  return CF_EXPECT(ParseBool(str, "flag"));
}

// Getter and Setter for std::vector
template <typename T>
std::string FlagValueToString(const std::vector<T>& values) {
  std::vector<std::string> strings;
  strings.reserve(values.size());
  for (const T& value : values) {
    strings.emplace_back(FlagValueToString(value));
  }
  return absl::StrJoin(strings, ",");
}
// Need an extra function to share between vector specializations because
// partial template specializations are not allowed in C++. It could be named
// ParseFlagValue too and just be an overload but using a different name makes
// it clear which implementation the compiler is choosing.
template <typename T>
Result<std::vector<T>> ParseFlagVectorValue(std::string_view str, T default_value) {
  if (str.empty()) {
    return {};
  }
  std::vector<std::string_view> elements = absl::StrSplit(str, ',');
  std::vector<T> parsed;
  parsed.reserve(elements.size());
  for (std::string_view element : elements) {
    if (element.empty()) {
      parsed.push_back(default_value);
    } else {
      parsed.push_back(CF_EXPECT(ParseFlagValue<T>(element)));
    }
  }
  return parsed;
}
template <>
Result<std::vector<std::string>> ParseFlagValue(std::string_view str) {
  return CF_EXPECT(ParseFlagVectorValue<std::string>(str, ""));
}
template <>
Result<std::vector<unsigned>> ParseFlagValue(std::string_view str) {
  return CF_EXPECT(ParseFlagVectorValue<unsigned>(str, 0));
}

// Getter and Setter for std::optional
template <typename T>
std::string FlagValueToString(const std::optional<T>& value,
                              CoerceToNullopt opt) {
  if (!value.has_value()) {
    return NullOptValue(opt);
  }
  return FlagValueToString(*value);
}
template <typename T>
Result<std::optional<T>> ParseFlagOptionalValue(std::string_view str,
                                                CoerceToNullopt opt) {
  if (ShouldBeNullOpt(str, opt)) {
    return std::nullopt;
  } else {
    return CF_EXPECT(ParseFlagValue<T>(str));
  }
}

// Template versions of the GflagsCompatFlag functions to reduce duplication
template <std::integral T>
static Flag GflagsCompatFlagImpl(const std::string& name, T& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return std::to_string(value); })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        if constexpr (std::is_unsigned_v<T>) {
          CF_EXPECTF(absl::SimpleAtoi<T>(match.value, &value),
                     "Failed to parse \"{}\" as an unsigned integer",
                     match.value);
        } else {
          CF_EXPECTF(absl::SimpleAtoi<T>(match.value, &value),
                     "Failed to parse \"{}\" as an integer", match.value);
        }
        return {};
      });
}

template <typename T>
Flag GflagsCompatFlagImpl(const std::string& name, std::vector<T>& value,
                          const T default_value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return FlagValueToString(value); })
      .Setter([name, &value,
               default_value](const FlagMatch& match) -> Result<void> {
        value = CF_EXPECT(ParseFlagVectorValue(match.value, default_value));
        return {};
      });
}

template <typename T>
Flag GflagsCompatFlagImpl(const std::string& name, std::optional<T>& value,
                          CoerceToNullopt opt) {
  return GflagsCompatFlag(name)
      .Getter([&value, opt]() { return FlagValueToString(value, opt); })
      .Setter([&value, opt](const FlagMatch& match) -> Result<void> {
        value = CF_EXPECT(ParseFlagOptionalValue<T>(match.value, opt));
        return {};
      });
}

std::string XmlEscape(const std::string& s) {
  return absl::StrReplaceAll(s, {{"<", "&lt;"}, {">", "&gt;"}});
}

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

Result<void> GflagsCompatBoolFlagSetter(const std::string& name,
                                               bool& value,
                                               const FlagMatch& match) {
  const auto& key = match.key;
  if (key == "-" + name || key == "--" + name) {
    value = true;
    return {};
  } else if (key == "-no" + name || key == "--no" + name) {
    value = false;
    return {};
  } else if (key == "-" + name + "=" || key == "--" + name + "=") {
    if (match.value == "true") {
      value = true;
      return {};
    } else if (match.value == "false") {
      value = false;
      return {};
    } else {
      return CF_ERRF("Unexpected boolean value \"{}\" for \{}\"", match.value,
                     name);
    }
  }
  return CF_ERRF("Unexpected key \"{}\" for \"{}\"", match.key, name);
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

Result<bool> ParseBool(std::string_view value, std::string_view name) {
  bool result;
  CF_EXPECTF(absl::SimpleAtob(value, &result),
             "Failed to parse value \"{}\" for {}", value, name);
  return result;
}

Result<int> ParseInt(const std::string& value, std::string_view name) {
  int result;
  CF_EXPECTF(absl::SimpleAtoi(value, &result),
             "Failed to parse value \"{}\" as integer for \"{}\"", value, name);
  return result;
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

bool Flag::WriteGflagsCompatXml(std::ostream& out) const {
  std::unordered_set<std::string> name_guesses;
  for (const auto& alias : aliases_) {
    std::string_view name = alias.name;
    if (!absl::ConsumePrefix(&name, "-")) {
      continue;
    }
    absl::ConsumePrefix(&name, "-");
    if (alias.mode == FlagAliasMode::kFlagExact) {
      absl::ConsumePrefix(&name, "no");
      name_guesses.insert(std::string{name});
    } else if (alias.mode == FlagAliasMode::kFlagConsumesFollowing) {
      name_guesses.insert(std::string{name});
    } else if (alias.mode == FlagAliasMode::kFlagPrefix) {
      if (!absl::ConsumeSuffix(&name, "=")) {
        continue;
      }
      name_guesses.insert(std::string{name});
    }
  }
  bool found_alias = false;
  for (const auto& name : name_guesses) {
    bool has_bool_aliases =
        HasAlias({FlagAliasMode::kFlagPrefix, "-" + name + "="}) &&
        HasAlias({FlagAliasMode::kFlagPrefix, "--" + name + "="}) &&
        HasAlias({FlagAliasMode::kFlagExact, "-" + name}) &&
        HasAlias({FlagAliasMode::kFlagExact, "--" + name}) &&
        HasAlias({FlagAliasMode::kFlagExact, "-no" + name}) &&
        HasAlias({FlagAliasMode::kFlagExact, "--no" + name});
    bool has_other_aliases =
        HasAlias({FlagAliasMode::kFlagPrefix, "-" + name + "="}) &&
        HasAlias({FlagAliasMode::kFlagPrefix, "--" + name + "="}) &&
        HasAlias({FlagAliasMode::kFlagConsumesFollowing, "-" + name}) &&
        HasAlias({FlagAliasMode::kFlagConsumesFollowing, "--" + name});
    bool has_help_aliases = HasAlias({FlagAliasMode::kFlagExact, "-help"}) &&
                            HasAlias({FlagAliasMode::kFlagExact, "--help"});
    std::vector<bool> has_aliases = {has_bool_aliases, has_other_aliases,
                                     has_help_aliases};
    const auto true_count =
        std::count(has_aliases.cbegin(), has_aliases.cend(), true);
    if (true_count > 1) {
      LOG(ERROR) << "Expected exactly one of has_bool_aliases, "
                 << "has_other_aliases, and has_help_aliases, got "
                 << true_count << " for \"" << name << "\".";
      return false;
    }
    if (true_count == 0) {
      continue;
    }
    found_alias = true;
    std::string type_str =
        (has_bool_aliases || has_help_aliases) ? "bool" : "string";
    // Lifted from external/gflags/src/gflags_reporting.cc:DescribeOneFlagInXML
    out << "<flag>\n";
    out << "  <file>file.cc</file>\n";
    out << "  <name>" << XmlEscape(name) << "</name>\n";
    auto help = help_ ? XmlEscape(*help_) : std::string{""};
    out << "  <meaning>" << help << "</meaning>\n";
    auto value = getter_ ? XmlEscape((*getter_)()) : std::string{""};
    out << "  <default>" << value << "</default>\n";
    out << "  <current>" << value << "</current>\n";
    out << "  <type>" << type_str << "</type>\n";
    out << "</flag>\n";
  }
  return found_alias;
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

std::vector<std::string> ArgsToVec(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(argc);
  for (int i = 0; i < argc; i++) {
    args.push_back(argv[i]);
  }
  return args;
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

bool WriteGflagsCompatXml(const std::vector<Flag>& flags, std::ostream& out) {
  for (const auto& flag : flags) {
    if (!flag.WriteGflagsCompatXml(out)) {
      return false;
    }
  }
  return true;
}

Flag VerbosityFlag(LogSeverity& value) {
  return GflagsCompatFlag("verbosity")
      .Getter([&value]() { return FromSeverity(value); })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        value = CF_EXPECT(ToSeverity(match.value));
        return {};
      })
      .Help("Used to set the verbosity level for logging.");
}

Flag HelpFlag(const std::vector<Flag>& flags, std::string text) {
  auto setter = [&flags, text](FlagMatch) -> Result<void> {
    if (!text.empty()) {
      LOG(INFO) << text;
    }
    for (const auto& flag : flags) {
      LOG(INFO) << flag;
    }
    // return value of 1 matches gflags --help flag behavior
    std::exit(1);
    return {};
  };
  return Flag("help")
      .Alias({FlagAliasMode::kFlagExact, "-help"})
      .Alias({FlagAliasMode::kFlagExact, "--help"})
      .Setter(setter);
}

Flag GflagsCompatBoolFlag(const std::string& name) {
  return Flag(name)
      .Alias({FlagAliasMode::kFlagPrefix, "-" + name + "="})
      .Alias({FlagAliasMode::kFlagPrefix, "--" + name + "="})
      .Alias({FlagAliasMode::kFlagExact, "-" + name})
      .Alias({FlagAliasMode::kFlagExact, "--" + name})
      .Alias({FlagAliasMode::kFlagExact, "-no" + name})
      .Alias({FlagAliasMode::kFlagExact, "--no" + name});
}

Flag HelpXmlFlag(const std::vector<Flag>& flags, std::ostream& out, bool& value,
                 std::string text) {
  const std::string name = "helpxml";
  auto setter = [name, &out, &value, text,
                 &flags](const FlagMatch& match) -> Result<void> {
    bool print_xml = false;
    CF_EXPECT(GflagsCompatBoolFlagSetter(name, print_xml, match));
    if (!print_xml) {
      return {};
    }
    if (!text.empty()) {
      out << text << std::endl;
    }
    value = print_xml;
    out << "<?xml version=\"1.0\"?>" << std::endl << "<AllFlags>" << std::endl;
    WriteGflagsCompatXml(flags, out);
    out << "</AllFlags>" << std::flush;
    return CF_ERR("Requested early exit");
  };
  return GflagsCompatBoolFlag(name).Setter(setter);
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

Flag GflagsCompatFlag(const std::string& name) {
  return Flag(name)
      .Alias({FlagAliasMode::kFlagPrefix, "-" + name + "="})
      .Alias({FlagAliasMode::kFlagPrefix, "--" + name + "="})
      .Alias({FlagAliasMode::kFlagConsumesFollowing, "-" + name})
      .Alias({FlagAliasMode::kFlagConsumesFollowing, "--" + name});
};

Flag GflagsCompatFlag(const std::string& name, std::string& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return value; })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        value = match.value;
        return {};
      });
}

Flag GflagsCompatFlag(const std::string& name, int32_t& value) {
  return GflagsCompatFlagImpl(name, value);
}

Flag GflagsCompatFlag(const std::string& name, size_t& value) {
  return GflagsCompatFlagImpl(name, value);
}

Flag GflagsCompatFlag(const std::string& name, bool& value) {
  return GflagsCompatBoolFlag(name)
      .Getter([&value]() { return fmt::format("{}", value); })
      .Setter([name, &value](const FlagMatch& match) {
        return GflagsCompatBoolFlagSetter(name, value, match);
      });
};

Flag GflagsCompatFlag(const std::string& name,
                      std::vector<std::string>& value) {
  return GflagsCompatFlagImpl<std::string>(name, value, "");
}

Flag GflagsCompatFlag(const std::string& name, std::vector<bool>& value,
                      const bool default_value) {
  return GflagsCompatFlagImpl(name, value, default_value);
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<std::string>& value,
                      CoerceToNullopt opt) {
  return GflagsCompatFlagImpl(name, value, opt);
}

Flag GflagsCompatFlag(const std::string& name, std::optional<size_t>& value,
                      CoerceToNullopt opt) {
  return GflagsCompatFlagImpl(name, value, opt);
}

Flag GflagsCompatFlag(const std::string& name, std::optional<unsigned>& value,
                      CoerceToNullopt opt) {
  return GflagsCompatFlagImpl(name, value, opt);
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<std::vector<std::string>>& value,
                      CoerceToNullopt opt) {
  return GflagsCompatFlagImpl(name, value, opt);
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<std::vector<unsigned>>& value,
                      CoerceToNullopt opt) {
  return GflagsCompatFlagImpl(name, value, opt);
}

Result<bool> HasHelpFlag(const std::vector<std::string>& args) {
  std::vector<std::string> copied_args(args);
  std::vector<Flag> flags;
  flags.reserve(help_bool_opts.size() + help_str_opts.size());
  bool bool_value_placeholder = false;
  std::string str_value_placeholder;
  for (const auto bool_opt : help_bool_opts) {
    flags.emplace_back(GflagsCompatFlag(bool_opt, bool_value_placeholder));
  }
  for (const auto str_opt : help_str_opts) {
    flags.emplace_back(GflagsCompatFlag(str_opt, str_value_placeholder));
  }
  CF_EXPECT(ConsumeFlags(flags, copied_args));
  // if there was any match, some in copied_args were consumed.
  return (args.size() != copied_args.size());
}

}  // namespace cuttlefish
