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

#include "cuttlefish/flag_parser/gflags_compat.h"

#include <concepts>
#include <functional>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include <fmt/format.h>
#include <fmt/ranges.h>  // NOLINT(misc-include-cleaner): version difference

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

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

// Setter for integral types, default getter is fine
template <std::integral T>
Result<T> ParseFlagValue(std::string_view str) {
  T value;
  CF_EXPECT(absl::SimpleAtoi(str, &value));
  return value;
}

// Setter for bool, default getter is fine
template<>
Result<bool> ParseFlagValue<bool>(std::string_view str) {
  bool result;
  CF_EXPECT(absl::SimpleAtob(str, &result));
  return result;
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
Flag GflagsCompatFlagImpl(const std::string& name, T& value) {
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

Result<void> GflagsCompatBoolFlagSetter(const std::string& name, bool& value,
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
      return CF_ERRF("Unexpected boolean value \"{}\" for \"{}\"", match.value,
                     name);
    }
  }
  return CF_ERRF("Unexpected key \"{}\" for \"{}\"", match.key, name);
}

Result<void> GflagsCompatBoolFlagSetter(const std::string& name,
                                        std::optional<bool>& value,
                                        const FlagMatch& match,
                                        CoerceToNullopt opt) {
  const auto& key = match.key;
  if (key == "-" + name || key == "--" + name) {
    value = true;
    return {};
  } else if (key == "-no" + name || key == "--no" + name) {
    value = false;
    return {};
  } else if (key == "-" + name + "=" || key == "--" + name + "=") {
    if (ShouldBeNullOpt(match.value, opt)) {
      value = std::nullopt;
      return {};
    }
    if (match.value == "true") {
      value = true;
      return {};
    } else if (match.value == "false") {
      value = false;
      return {};
    } else {
      return CF_ERRF("Unexpected boolean value \"{}\" for \"{}\"", match.value,
                     name);
    }
  }
  return CF_ERRF("Unexpected key \"{}\" for \"{}\"", match.key, name);
}

}  // namespace

bool WriteGflagsCompatXml(const Flag& flag, std::ostream& out) {
  std::unordered_set<std::string> name_guesses;
  for (const auto& alias : flag.aliases_) {
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
        flag.HasAlias({FlagAliasMode::kFlagPrefix, "-" + name + "="}) &&
        flag.HasAlias({FlagAliasMode::kFlagPrefix, "--" + name + "="}) &&
        flag.HasAlias({FlagAliasMode::kFlagExact, "-" + name}) &&
        flag.HasAlias({FlagAliasMode::kFlagExact, "--" + name}) &&
        flag.HasAlias({FlagAliasMode::kFlagExact, "-no" + name}) &&
        flag.HasAlias({FlagAliasMode::kFlagExact, "--no" + name});
    bool has_other_aliases =
        flag.HasAlias({FlagAliasMode::kFlagPrefix, "-" + name + "="}) &&
        flag.HasAlias({FlagAliasMode::kFlagPrefix, "--" + name + "="}) &&
        flag.HasAlias({FlagAliasMode::kFlagConsumesFollowing, "-" + name}) &&
        flag.HasAlias({FlagAliasMode::kFlagConsumesFollowing, "--" + name});
    bool has_help_aliases = flag.HasAlias({FlagAliasMode::kFlagExact, "-help"}) &&
                            flag.HasAlias({FlagAliasMode::kFlagExact, "--help"});
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
    auto help = flag.help_ ? XmlEscape(*flag.help_) : std::string{""};
    out << "  <meaning>" << help << "</meaning>\n";
    auto value = flag.getter_ ? XmlEscape((*flag.getter_)()) : std::string{""};
    out << "  <default>" << value << "</default>\n";
    out << "  <current>" << value << "</current>\n";
    out << "  <type>" << type_str << "</type>\n";
    out << "</flag>\n";
  }
  return found_alias;
}

bool WriteGflagsCompatXml(const std::vector<Flag>& flags, std::ostream& out) {
  for (const auto& flag : flags) {
    if (!WriteGflagsCompatXml(flag, out)) {
      return false;
    }
  }
  return true;
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

Flag GflagsCompatFlag(const std::string& name) {
  return Flag(name)
      .Alias({FlagAliasMode::kFlagPrefix, "-" + name + "="})
      .Alias({FlagAliasMode::kFlagPrefix, "--" + name + "="})
      .Alias({FlagAliasMode::kFlagConsumesFollowing, "-" + name})
      .Alias({FlagAliasMode::kFlagConsumesFollowing, "--" + name});
}

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
}

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

Flag GflagsCompatFlag(const std::string& name, std::optional<int>& value,
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

Flag GflagsCompatFlag(const std::string& name, std::optional<int64_t>& value,
                      CoerceToNullopt opt) {
  return GflagsCompatFlagImpl(name, value, opt);
}

Flag GflagsCompatFlag(const std::string& name, std::optional<bool>& value,
                      CoerceToNullopt opt) {
  return GflagsCompatBoolFlag(name)
      .Getter([&value, opt]() { return FlagValueToString(value, opt); })
      .Setter([name, &value, opt](const FlagMatch& match) {
        return GflagsCompatBoolFlagSetter(name, value, match, opt);
      });
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

}  // namespace cuttlefish
