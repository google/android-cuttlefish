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

#include "host/commands/cvd/cli/selector/arguments_lexer.h"

#include <algorithm>
#include <regex>
#include <vector>

#include <android-base/strings.h>

#include "host/commands/cvd/cli/selector/selector_constants.h"
#include "host/commands/cvd/instances/instance_database_utils.h"

namespace cuttlefish {
namespace selector {
namespace {

class ArgumentsLexer {
 public:
  static Result<std::unique_ptr<ArgumentsLexer>> Build();

  Result<std::vector<ArgToken>> Tokenize(
      const std::vector<std::string>& args) const;

 private:
  // Lexer factory function will internally generate this,
  // and give it to ArgumentsLexer.
  struct FlagPatterns {
    /* represents flags that takes values
     * e.g. -group_name, --group_name (which may take an additional
     * positional arg, or use its default value.)
     *
     * With the given example, this set shall be:
     *  {"-group_name", "--group_name"}
     */
    std::unordered_set<std::string> value_patterns;
  };
  ArgumentsLexer(FlagPatterns&& flag_patterns);

  Result<ArgToken> Process(const std::string& token) const;

  struct FlagValuePair {
    std::string flag_string;
    std::string value;
  };
  Result<FlagValuePair> Separate(
      const std::string& equal_included_string) const;
  // flag_string starts with "-" or "--"
  static bool Registered(const std::string& flag_string,
                         const FlagPatterns& flag_patterns);
  bool Registered(const std::string& flag_string) const {
    return Registered(flag_string, flag_patterns_);
  }
  FlagPatterns flag_patterns_;
};

/*
 * At the top level, there are only two tokens: flag and positional tokens.
 *
 * A flag token starts with "-" or "--" followed by one or more non "-" letters.
 * A positional token starts with any character other than "-".
 *
 * Between flag tokens, there are "known" and "unknown" flag tokens.
 *
 * Eventually, we get two sets, each include strings start with "-" or "--".
 *
 * Say, the two sets are BaseSet and NoPrependedSet.
 *
 * Given a non boolean flag --bar, these will happen:
 *   BaseSet = BaseSet U {"--bar", "-bar"}
 *
 * Later on, when the parser reads a token, the parser will look up the
 * two sets to see if the token that is supposedly a flag is a known
 * flag.
 */
Result<std::unique_ptr<ArgumentsLexer>> ArgumentsLexer::Build() {
  // Change together: ParseCommonSelectorArguments in selector_common_parser.cpp
  std::unordered_set<std::string> known_flags{SelectorFlags::kGroupName,
                                              SelectorFlags::kInstanceName,
                                              SelectorFlags::kVerbosity};

  FlagPatterns flag_patterns;
  for (const auto& non_bool_flag : known_flags) {
    const auto one_dash = "-" + non_bool_flag;
    const auto two_dashes = "--" + non_bool_flag;
    CF_EXPECT(!ArgumentsLexer::Registered(one_dash, flag_patterns));
    CF_EXPECT(!ArgumentsLexer::Registered(two_dashes, flag_patterns));
    flag_patterns.value_patterns.insert(one_dash);
    flag_patterns.value_patterns.insert(two_dashes);
  }

  ArgumentsLexer* new_lexer = new ArgumentsLexer(std::move(flag_patterns));
  CF_EXPECT(new_lexer != nullptr,
            "Memory allocation for ArgumentsLexer failed.");
  return std::unique_ptr<ArgumentsLexer>{new_lexer};
}

ArgumentsLexer::ArgumentsLexer(FlagPatterns&& flag_patterns)
    : flag_patterns_{std::move(flag_patterns)} {}

bool ArgumentsLexer::Registered(const std::string& flag_string,
                                const FlagPatterns& flag_patterns) {
  return Contains(flag_patterns.value_patterns, flag_string);
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
  return ArgToken{ArgType::kUnknownFlag, token};
}

Result<std::vector<ArgToken>> ArgumentsLexer::Tokenize(
    const std::vector<std::string>& args) const {
  std::vector<ArgToken> tokenized;
  for (const auto& arg : args) {
    auto arg_token = CF_EXPECT(Process(arg));
    tokenized.emplace_back(arg_token);
  }
  return tokenized;
}

Result<ArgumentsLexer::FlagValuePair> ArgumentsLexer::Separate(
    const std::string& equal_included_string) const {
  CF_EXPECT(Contains(equal_included_string, "="));
  auto equal_sign_pos = equal_included_string.find_first_of('=');
  auto first_token = equal_included_string.substr(0, equal_sign_pos);
  auto second_token = equal_included_string.substr(equal_sign_pos + 1);
  return FlagValuePair{.flag_string = first_token, .value = second_token};
}

}  // namespace

Result<std::vector<ArgToken>> TokenizeArguments(
    const std::vector<std::string>& args) {
  std::unique_ptr<ArgumentsLexer> lexer = CF_EXPECT(ArgumentsLexer::Build());
  CF_EXPECT(lexer.get());

  return CF_EXPECT(lexer->Tokenize(args));
}

}  // namespace selector
}  // namespace cuttlefish
