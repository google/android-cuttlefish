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

#include "common/libs/utils/flag_parser.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <android-base/logging.h>
#include <android-base/strings.h>

namespace cuttlefish {

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
  using android::base::EndsWith;
  using android::base::StartsWith;

  CHECK(StartsWith(alias.name, "-")) << "Flags should start with \"-\"";
  if (alias.mode == FlagAliasMode::kFlagPrefix) {
    CHECK(EndsWith(alias.name, "=")) << "Prefix flags shold end with \"=\"";
  }

  CHECK(!HasAlias(alias)) << "Duplicate flag alias: " << alias.name;
  if (alias.mode == FlagAliasMode::kFlagConsumesFollowing) {
    CHECK(!HasAlias({FlagAliasMode::kFlagExact, alias.name}))
        << "Overlapping flag aliases for " << alias.name;
    CHECK(!HasAlias({FlagAliasMode::kFlagConsumesArbitrary, alias.name}))
        << "Overlapping flag aliases for " << alias.name;
  } else if (alias.mode == FlagAliasMode::kFlagExact) {
    CHECK(!HasAlias({FlagAliasMode::kFlagConsumesFollowing, alias.name}))
        << "Overlapping flag aliases for " << alias.name;
    CHECK(!HasAlias({FlagAliasMode::kFlagConsumesArbitrary, alias.name}))
        << "Overlapping flag aliases for " << alias.name;
  } else if (alias.mode == FlagAliasMode::kFlagConsumesArbitrary) {
    CHECK(!HasAlias({FlagAliasMode::kFlagExact, alias.name}))
        << "Overlapping flag aliases for " << alias.name;
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

Flag& Flag::Setter(std::function<bool(const FlagMatch&)> fn) & {
  setter_ = std::move(fn);
  return *this;
}
Flag Flag::Setter(std::function<bool(const FlagMatch&)> fn) && {
  setter_ = std::move(fn);
  return *this;
}

static bool LikelyFlag(const std::string& next_arg) {
  return android::base::StartsWith(next_arg, "-");
}

std::string BoolToString(bool val) {
  return val ? "true" : "false";
}

Flag::FlagProcessResult Flag::Process(
    const std::string& arg, const std::optional<std::string>& next_arg) const {
  if (!setter_ && aliases_.size() > 0) {
    LOG(ERROR) << "No setter for flag with alias " << aliases_[0].name;
    return FlagProcessResult::kFlagError;
  }
  for (auto& alias : aliases_) {
    switch (alias.mode) {
      case FlagAliasMode::kFlagConsumesArbitrary:
        if (arg != alias.name) {
          continue;
        }
        if (!next_arg || LikelyFlag(*next_arg)) {
          if (!(*setter_)({arg, ""})) {
            LOG(ERROR) << "Processing \"" << arg << "\" failed";
            return FlagProcessResult::kFlagError;
          }
          return FlagProcessResult::kFlagConsumed;
        }
        if (!(*setter_)({arg, *next_arg})) {
          LOG(ERROR) << "Processing \"" << arg << "\" \"" << *next_arg
                     << "\" failed";
          return FlagProcessResult::kFlagError;
        }
        return FlagProcessResult::kFlagConsumedOnlyFollowing;
      case FlagAliasMode::kFlagConsumesFollowing:
        if (arg != alias.name) {
          continue;
        }
        if (!next_arg) {
          LOG(ERROR) << "Expected an argument after \"" << arg << "\"";
          return FlagProcessResult::kFlagError;
        }
        if (!(*setter_)({arg, *next_arg})) {
          LOG(ERROR) << "Processing \"" << arg << "\" \"" << *next_arg
                     << "\" failed";
          return FlagProcessResult::kFlagError;
        }
        return FlagProcessResult::kFlagConsumedWithFollowing;
      case FlagAliasMode::kFlagExact:
        if (arg != alias.name) {
          continue;
        }
        if (!(*setter_)({arg, arg})) {
          LOG(ERROR) << "Processing \"" << arg << "\" failed";
          return FlagProcessResult::kFlagError;
        }
        return FlagProcessResult::kFlagConsumed;
      case FlagAliasMode::kFlagPrefix:
        if (!android::base::StartsWith(arg, alias.name)) {
          continue;
        }
        if (!(*setter_)({alias.name, arg.substr(alias.name.size())})) {
          LOG(ERROR) << "Processing \"" << arg << "\" failed";
          return FlagProcessResult::kFlagError;
        }
        return FlagProcessResult::kFlagConsumed;
      default:
        LOG(ERROR) << "Unknown flag alias mode: " << (int)alias.mode;
        return FlagProcessResult::kFlagError;
    }
  }
  return FlagProcessResult::kFlagSkip;
}

bool Flag::Parse(std::vector<std::string>& arguments) const {
  for (int i = 0; i < arguments.size();) {
    std::string arg = arguments[i];
    std::optional<std::string> next_arg;
    if (i < arguments.size() - 1) {
      next_arg = arguments[i + 1];
    }
    auto result = Process(arg, next_arg);
    if (result == FlagProcessResult::kFlagError) {
      return false;
    } else if (result == FlagProcessResult::kFlagConsumed) {
      arguments.erase(arguments.begin() + i);
    } else if (result == FlagProcessResult::kFlagConsumedWithFollowing) {
      arguments.erase(arguments.begin() + i, arguments.begin() + i + 2);
    } else if (result == FlagProcessResult::kFlagConsumedOnlyFollowing) {
      arguments.erase(arguments.begin() + i + 1, arguments.begin() + i + 2);
    } else if (result == FlagProcessResult::kFlagSkip) {
      i++;
      continue;
    } else {
      LOG(ERROR) << "Unknown FlagProcessResult: " << (int)result;
      return false;
    }
  }
  return true;
}
bool Flag::Parse(std::vector<std::string>&& arguments) const {
  return Parse(static_cast<std::vector<std::string>&>(arguments));
}

bool Flag::HasAlias(const FlagAlias& test) const {
  for (const auto& alias : aliases_) {
    if (alias.mode == test.mode && alias.name == test.name) {
      return true;
    }
  }
  return false;
}

static std::string XmlEscape(const std::string& s) {
  using android::base::StringReplace;
  return StringReplace(StringReplace(s, "<", "&lt;", true), ">", "&gt;", true);
}

bool Flag::WriteGflagsCompatXml(std::ostream& out) const {
  std::unordered_set<std::string> name_guesses;
  for (const auto& alias : aliases_) {
    std::string_view name = alias.name;
    if (!android::base::ConsumePrefix(&name, "-")) {
      continue;
    }
    android::base::ConsumePrefix(&name, "-");
    if (alias.mode == FlagAliasMode::kFlagExact) {
      android::base::ConsumePrefix(&name, "no");
      name_guesses.insert(std::string{name});
    } else if (alias.mode == FlagAliasMode::kFlagConsumesFollowing) {
      name_guesses.insert(std::string{name});
    } else if (alias.mode == FlagAliasMode::kFlagPrefix) {
      if (!android::base::ConsumeSuffix(&name, "=")) {
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
  out << "[";
  for (auto it = flag.aliases_.begin(); it != flag.aliases_.end(); it++) {
    if (it != flag.aliases_.begin()) {
      out << ", ";
    }
    out << *it;
  }
  out << "]\n";
  if (flag.help_) {
    out << "(" << *flag.help_ << ")\n";
  }
  if (flag.getter_) {
    out << "(Current value: \"" << (*flag.getter_)() << "\")\n";
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

bool ParseFlags(const std::vector<Flag>& flags,
                std::vector<std::string>& args) {
  for (const auto& flag : flags) {
    if (!flag.Parse(args)) {
      return false;
    }
  }
  return true;
}

bool ParseFlags(const std::vector<Flag>& flags,
                std::vector<std::string>&& args) {
  for (const auto& flag : flags) {
    if (!flag.Parse(args)) {
      return false;
    }
  }
  return true;
}

bool WriteGflagsCompatXml(const std::vector<Flag>& flags, std::ostream& out) {
  for (const auto& flag : flags) {
    if (!flag.WriteGflagsCompatXml(out)) {
      return false;
    }
  }
  return true;
}

Flag HelpFlag(const std::vector<Flag>& flags, const std::string& text) {
  auto setter = [&](FlagMatch) {
    if (text.size() > 0) {
      LOG(INFO) << text;
    }
    for (const auto& flag : flags) {
      LOG(INFO) << flag;
    }
    return false;
  };
  return Flag()
      .Alias({FlagAliasMode::kFlagExact, "-help"})
      .Alias({FlagAliasMode::kFlagExact, "--help"})
      .Setter(setter);
}

static bool GflagsCompatBoolFlagSetter(const std::string& name, bool& value,
                                       const FlagMatch& match) {
  const auto& key = match.key;
  if (key == "-" + name || key == "--" + name) {
    value = true;
    return true;
  } else if (key == "-no" + name || key == "--no" + name) {
    value = false;
    return true;
  } else if (key == "-" + name + "=" || key == "--" + name + "=") {
    if (match.value == "true") {
      value = true;
      return true;
    } else if (match.value == "false") {
      value = false;
      return true;
    } else {
      LOG(ERROR) << "Unexpected boolean value \"" << match.value << "\""
                 << " for \"" << name << "\"";
      return false;
    }
  }
  LOG(ERROR) << "Unexpected key \"" << match.key << "\""
             << " for \"" << name << "\"";
  return false;
}

static Flag GflagsCompatBoolFlagBase(const std::string& name) {
  return Flag()
      .Alias({FlagAliasMode::kFlagPrefix, "-" + name + "="})
      .Alias({FlagAliasMode::kFlagPrefix, "--" + name + "="})
      .Alias({FlagAliasMode::kFlagExact, "-" + name})
      .Alias({FlagAliasMode::kFlagExact, "--" + name})
      .Alias({FlagAliasMode::kFlagExact, "-no" + name})
      .Alias({FlagAliasMode::kFlagExact, "--no" + name});
}

Flag HelpXmlFlag(const std::vector<Flag>& flags, std::ostream& out, bool& value,
                 const std::string& text) {
  const std::string name = "helpxml";
  auto setter = [name, &out, &value, &text, &flags](const FlagMatch& match) {
    bool print_xml = false;
    auto parse_success = GflagsCompatBoolFlagSetter(name, print_xml, match);
    if (!parse_success) {
      return false;
    }
    if (!print_xml) {
      return true;
    }
    if (!text.empty()) {
      out << text << std::endl;
    }
    value = print_xml;
    out << "<?xml version=\"1.0\"?>" << std::endl << "<AllFlags>" << std::endl;
    WriteGflagsCompatXml(flags, out);
    out << "</AllFlags>" << std::flush;
    return false;
  };
  return GflagsCompatBoolFlagBase(name).Setter(setter);
}

Flag InvalidFlagGuard() {
  return Flag()
      .UnvalidatedAlias({FlagAliasMode::kFlagPrefix, "-"})
      .Help(
          "This executable only supports the flags in `-help`. Positional "
          "arguments may be supported.")
      .Setter([](const FlagMatch& match) {
        LOG(ERROR) << "Unknown flag " << match.value;
        return false;
      });
}

Flag UnexpectedArgumentGuard() {
  return Flag()
      .UnvalidatedAlias({FlagAliasMode::kFlagPrefix, ""})
      .Help(
          "This executable only supports the flags in `-help`. Positional "
          "arguments are not supported.")
      .Setter([](const FlagMatch& match) {
        LOG(ERROR) << "Unexpected argument \"" << match.value << "\"";
        return false;
      });
}

Flag GflagsCompatFlag(const std::string& name) {
  return Flag()
      .Alias({FlagAliasMode::kFlagPrefix, "-" + name + "="})
      .Alias({FlagAliasMode::kFlagPrefix, "--" + name + "="})
      .Alias({FlagAliasMode::kFlagConsumesFollowing, "-" + name})
      .Alias({FlagAliasMode::kFlagConsumesFollowing, "--" + name});
};

Flag GflagsCompatFlag(const std::string& name, std::string& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return value; })
      .Setter([&value](const FlagMatch& match) {
        value = match.value;
        return true;
      });
}

template <typename T>
std::optional<T> ParseInteger(const std::string& value) {
  if (value.size() == 0) {
    return {};
  }
  const char* base = value.c_str();
  char* end = nullptr;
  errno = 0;
  auto r = strtoll(base, &end, /* auto-detect */ 0);
  if (errno != 0 || end != base + value.size()) {
    return {};
  }
  if (static_cast<T>(r) != r) {
    return {};
  }
  return r;
}

template <typename T>
static Flag GflagsCompatNumericFlagGeneric(const std::string& name, T& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return std::to_string(value); })
      .Setter([&value](const FlagMatch& match) {
        auto parsed = ParseInteger<T>(match.value);
        if (parsed) {
          value = *parsed;
          return true;
        } else {
          LOG(ERROR) << "Failed to parse \"" << match.value
                     << "\" as an integer";
          return false;
        }
      });
}

Flag GflagsCompatFlag(const std::string& name, int32_t& value) {
  return GflagsCompatNumericFlagGeneric(name, value);
}

Flag GflagsCompatFlag(const std::string& name, bool& value) {
  return GflagsCompatBoolFlagBase(name)
      .Getter([&value]() { return value ? "true" : "false"; })
      .Setter([name, &value](const FlagMatch& match) {
        return GflagsCompatBoolFlagSetter(name, value, match);
      });
};

}  // namespace cuttlefish
