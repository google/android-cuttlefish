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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "fmt/format.h"

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
template <>
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
Result<std::vector<T>> ParseFlagVectorValue(std::string_view str,
                                            T default_value) {
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
  return Flag::StringFlag(name)
      .Getter([&value]() { return std::to_string(value); })
      .Setter([&value](std::string_view arg) -> Result<void> {
        if constexpr (std::is_unsigned_v<T>) {
          CF_EXPECTF(absl::SimpleAtoi<T>(arg, &value),
                     "Failed to parse \"{}\" as an unsigned integer", arg);
        } else {
          CF_EXPECTF(absl::SimpleAtoi<T>(arg, &value),
                     "Failed to parse \"{}\" as an integer", arg);
        }
        return {};
      });
}

template <typename T>
Flag GflagsCompatFlagImpl(const std::string& name, std::vector<T>& value,
                          const T default_value) {
  return Flag::StringFlag(name)
      .Getter([&value]() { return FlagValueToString(value); })
      .Setter(
          [name, &value, default_value](std::string_view arg) -> Result<void> {
            value = CF_EXPECT(ParseFlagVectorValue(arg, default_value));
            return {};
          });
}

template <typename T>
Flag GflagsCompatFlagImpl(const std::string& name, std::optional<T>& value,
                          CoerceToNullopt opt) {
  return Flag::StringFlag(name)
      .Getter([&value, opt]() { return FlagValueToString(value, opt); })
      .Setter([&value, opt](std::string_view arg) -> Result<void> {
        value = CF_EXPECT(ParseFlagOptionalValue<T>(arg, opt));
        return {};
      });
}

std::string XmlEscape(const std::string& s) {
  return absl::StrReplaceAll(s, {{"<", "&lt;"}, {">", "&gt;"}});
}

Flag WithVectorNameValueHint(Flag flag) {
  std::string hint = flag.ValueNameHint();
  return std::move(flag).ValueNameHint(absl::StrCat(hint, "[,", hint, "...]"));
}

}  // namespace

void WriteGflagsCompatXml(const Flag& flag, std::ostream& out) {
  std::string type_str;
  switch (flag.style_) {
    case Flag::Style::String:
      type_str = "string";
      break;
    case Flag::Style::Bool:
      type_str = "bool";
      break;
  }
  // Lifted from external/gflags/src/gflags_reporting.cc:DescribeOneFlagInXML
  out << "<flag>\n";
  out << "  <file>file.cc</file>\n";
  out << "  <name>" << flag.Name() << "</name>\n";
  out << "  <meaning>" << XmlEscape(flag.help_) << "</meaning>\n";
  auto value = XmlEscape(flag.getter_());
  out << "  <default>" << value << "</default>\n";
  out << "  <current>" << value << "</current>\n";
  out << "  <type>" << type_str << "</type>\n";
  out << "</flag>\n";
}

void WriteGflagsCompatXml(const std::vector<Flag>& flags, std::ostream& out) {
  for (const auto& flag : flags) {
    WriteGflagsCompatXml(flag, out);
  }
}

Flag HelpFlag(const std::vector<Flag>& flags, std::string text) {
  auto setter = [&flags, text](std::string_view) -> Result<void> {
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
  return Flag::BoolFlag("help").Setter(setter);
}

Flag HelpXmlFlag(const std::vector<Flag>& flags, std::ostream& out,
                 bool& print_xml, std::string text) {
  const std::string name = "helpxml";
  auto setter = [name, &out, &print_xml, text,
                 &flags](std::string_view arg) -> Result<void> {
    print_xml = CF_EXPECTF(ParseFlagValue<bool>(arg),
                           "Unexpected value for '--helpxml': '{}'", arg);
    if (!print_xml) {
      return {};
    }
    if (!text.empty()) {
      out << text << std::endl;
    }
    out << "<?xml version=\"1.0\"?>" << std::endl << "<AllFlags>" << std::endl;
    WriteGflagsCompatXml(flags, out);
    out << "</AllFlags>\n" << std::flush;
    // return value of 1 matches gflags --help flag behavior
    std::exit(1);
    return {};
  };
  return Flag::BoolFlag(name).Setter(setter);
}

Flag GflagsCompatFlag(const std::string& name, std::string& value) {
  return Flag::StringFlag(name)
      .Getter([&value]() { return value; })
      .Setter([&value](std::string_view arg) -> Result<void> {
        value = arg;
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
  return Flag::BoolFlag(name)
      .Getter([&value]() { return fmt::format("{}", value); })
      .Setter([name, &value](std::string_view arg) -> Result<void> {
        value = CF_EXPECTF(ParseFlagValue<bool>(arg),
                           "Unexpected value for \"--{}\": \"{}\"", arg, name);
        return {};
      });
}

Flag GflagsCompatFlag(const std::string& name,
                      std::vector<std::string>& value) {
  return WithVectorNameValueHint(
      GflagsCompatFlagImpl<std::string>(name, value, ""));
}

Flag GflagsCompatFlag(const std::string& name, std::vector<unsigned>& value) {
  return WithVectorNameValueHint(
      GflagsCompatFlagImpl<unsigned>(name, value, 0));
}

Flag GflagsCompatFlag(const std::string& name, std::vector<bool>& value,
                      const bool default_value) {
  return WithVectorNameValueHint(
      GflagsCompatFlagImpl(name, value, default_value));
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<std::string>& value, CoerceToNullopt opt) {
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
  return Flag::BoolFlag(name)
      .Getter([&value]() -> std::string {
        return value ? fmt::format("{}", *value) : "";
      })
      .Setter([name, &value, opt](std::string_view arg) -> Result<void> {
        value = CF_EXPECT(ParseFlagOptionalValue<bool>(arg, opt));
        return {};
      });
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<std::vector<std::string>>& value,
                      CoerceToNullopt opt) {
  return WithVectorNameValueHint(GflagsCompatFlagImpl(name, value, opt));
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<std::vector<unsigned>>& value,
                      CoerceToNullopt opt) {
  return WithVectorNameValueHint(GflagsCompatFlagImpl(name, value, opt));
}

}  // namespace cuttlefish
