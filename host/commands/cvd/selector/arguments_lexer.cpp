/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/selector/arguments_lexer.h"

#include <algorithm>
#include <regex>
#include <vector>

#include <android-base/strings.h>

#include "host/commands/cvd/selector/instance_database_utils.h"

namespace cuttlefish {
namespace selector {
namespace {

template <typename... Sets>
bool Included(const std::string& item, Sets&&... containers) {
  return ((Contains(std::forward<Sets>(containers), item)) || ... || false);
}

}  // namespace

/*
 * Eventually, we get two sets, each include strings start with "-" or "--".
 *
 * Say, the two sets are BaseSet and NoPrependedSet.
 *
 * Given a boolean flag --foo, these will happen:
 *   BaseSet = BaseSet U {"--foo", "-foo"}
 *   NoPrependedSet = NoPrependedSet U  {"--nofoo", "-nofoo"}
 * Given a non boolean flag --bar, these will happen:
 *   BaseSet = BaseSet U {"--bar", "-bar"}
 *
 * Later on, when the parser reads a token, the parser will look up the
 * two sets to see if the token that is supposedly a flag is a known
 * flag.
 */
Result<ArgumentsLexerBuilder::FlagPatterns>
ArgumentsLexerBuilder::GenerateFlagPatterns(
    const LexerFlagsSpecification& known_flags) {
  FlagPatterns flag_patterns;
  for (const auto& non_bool_flag : known_flags.known_value_flags) {
    const auto one_dash = "-" + non_bool_flag;
    const auto two_dashes = "--" + non_bool_flag;
    CF_EXPECT(!ArgumentsLexer::Registered(one_dash, flag_patterns));
    CF_EXPECT(!ArgumentsLexer::Registered(two_dashes, flag_patterns));
    flag_patterns.value_patterns.insert(one_dash);
    flag_patterns.value_patterns.insert(two_dashes);
  }
  for (const auto& bool_flag : known_flags.known_boolean_flags) {
    const auto one_dash = "-" + bool_flag;
    const auto two_dashes = "--" + bool_flag;
    const auto one_dash_with_no = "-no" + bool_flag;
    const auto two_dashes_with_no = "--no" + bool_flag;
    CF_EXPECT(!ArgumentsLexer::Registered(one_dash, flag_patterns));
    CF_EXPECT(!ArgumentsLexer::Registered(two_dashes, flag_patterns));
    CF_EXPECT(!ArgumentsLexer::Registered(one_dash_with_no, flag_patterns));
    CF_EXPECT(!ArgumentsLexer::Registered(two_dashes_with_no, flag_patterns));
    flag_patterns.bool_patterns.insert(one_dash);
    flag_patterns.bool_patterns.insert(two_dashes);
    flag_patterns.bool_no_patterns.insert(one_dash_with_no);
    flag_patterns.bool_no_patterns.insert(two_dashes_with_no);
  }
  return flag_patterns;
}

Result<std::unique_ptr<ArgumentsLexer>> ArgumentsLexerBuilder::Build(
    const LexerFlagsSpecification& known_flags) {
  auto flag_patterns = CF_EXPECT(GenerateFlagPatterns(known_flags));
  ArgumentsLexer* new_lexer = new ArgumentsLexer(std::move(flag_patterns));
  CF_EXPECT(new_lexer != nullptr,
            "Memory allocation for ArgumentsLexer failed.");
  return std::unique_ptr<ArgumentsLexer>{new_lexer};
}

ArgumentsLexer::ArgumentsLexer(FlagPatterns&& flag_patterns)
    : flag_patterns_{std::move(flag_patterns)} {
  valid_bool_values_in_lower_cases_ =
      std::unordered_set<std::string>{"true", "false", "yes", "no", "y", "n"};
}

bool ArgumentsLexer::Registered(const std::string& flag_string,
                                const FlagPatterns& flag_patterns) {
  return Included(flag_string, flag_patterns.value_patterns,
                  flag_patterns.bool_patterns, flag_patterns.bool_no_patterns);
}

Result<ArgToken> ArgumentsLexer::Process(const std::string& token) const {
  if (token == "--") {
    return ArgToken{ArgType::kDoubleDash, token};
  }
  std::regex flag_and_value_pattern("[\\-][\\-]?[^\\-]+.*=.*");
  std::regex flag_pattern("[\\-][\\-]?[^\\-]+.*");
  std::regex base_pattern("[^\\-]+.*");
  if (std::regex_match(token, base_pattern)) {
    return ArgToken{ArgType::kPositional, token};
  }
  if (!std::regex_match(token, flag_pattern)) {
    return ArgToken{ArgType::kError, token};
  }
  // --flag=value
  if (std::regex_match(token, flag_and_value_pattern)) {
    auto [flag_string, value] = CF_EXPECT(Separate(token));
    // is --flag registered?
    if (Contains(flag_patterns_.value_patterns, flag_string)) {
      return ArgToken{ArgType::kKnownFlagAndValue, token};
    }
    return ArgToken{ArgType::kUnknownFlag, token};
  }
  if (Contains(flag_patterns_.value_patterns, token)) {
    return ArgToken{ArgType::kKnownValueFlag, token};
  }
  if (Contains(flag_patterns_.bool_patterns, token)) {
    return ArgToken{ArgType::kKnownBoolFlag, token};
  }
  if (Contains(flag_patterns_.bool_no_patterns, token)) {
    return ArgToken{ArgType::kKnownBoolNoFlag, token};
  }
  return ArgToken{ArgType::kUnknownFlag, token};
}

Result<std::vector<ArgToken>> ArgumentsLexer::Tokenize(
    const std::vector<std::string>& args) const {
  std::vector<ArgToken> tokenized;
  auto intersection =
      Intersection(flag_patterns_.value_patterns, flag_patterns_.bool_patterns);
  CF_EXPECT(intersection.empty());
  auto preprocessed_args = CF_EXPECT(Preprocess(args));
  for (const auto& arg : preprocessed_args) {
    auto arg_token = CF_EXPECT(Process(arg));
    tokenized.emplace_back(arg_token);
  }
  return tokenized;
}

static std::string ToLower(const std::string& src) {
  std::string lower_cased_value;
  lower_cased_value.resize(src.size());
  std::transform(src.begin(), src.end(), lower_cased_value.begin(), ::tolower);
  return lower_cased_value;
}

Result<ArgumentsLexer::FlagValuePair> ArgumentsLexer::Separate(
    const std::string& equal_included_string) const {
  CF_EXPECT(Contains(equal_included_string, "="));
  auto equal_sign_pos = equal_included_string.find_first_of('=');
  auto first_token = equal_included_string.substr(0, equal_sign_pos);
  auto second_token = equal_included_string.substr(equal_sign_pos + 1);
  return FlagValuePair{.flag_string = first_token, .value = second_token};
}

Result<std::vector<std::string>> ArgumentsLexer::Preprocess(
    const std::vector<std::string>& args) const {
  std::vector<std::string> new_args;
  std::regex pattern("[\\-][\\-]?[^\\-]+.*=.*");
  for (const auto& arg : args) {
    if (!std::regex_match(arg, pattern)) {
      new_args.emplace_back(arg);
      continue;
    }
    // needs to split based on the first '='
    // --something=another_thing or
    //  -something=another_thing
    const auto [flag_string, value] = CF_EXPECT(Separate(arg));

    if (Contains(flag_patterns_.bool_patterns, flag_string)) {
      const auto low_cased_value = ToLower(value);
      CF_EXPECT(Contains(valid_bool_values_in_lower_cases_, low_cased_value),
                "The value for the boolean flag " << flag_string << ", "
                                                  << value << " is not valid");
      if (low_cased_value == "true" || low_cased_value == "yes") {
        new_args.emplace_back(flag_string);
        continue;
      }
      auto base_pos = flag_string.find_first_not_of('-');
      auto base = flag_string.substr(base_pos);
      new_args.emplace_back("--no" + base);
      continue;
    }

    if (Contains(flag_patterns_.bool_no_patterns, flag_string)) {
      CF_EXPECT(android::base::StartsWith(flag_string, "-no") ||
                android::base::StartsWith(flag_string, "--no"));
      // if --nohelp=XYZ, the "=XYZ" is ignored.
      new_args.emplace_back(flag_string);
      continue;
    }

    new_args.emplace_back(arg);
  }
  return new_args;
}

}  // namespace selector
}  // namespace cuttlefish
