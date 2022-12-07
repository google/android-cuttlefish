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

#pragma once

#include <memory>
#include <string>
#include <unordered_set>

#include <cvd_server.pb.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace selector {

/**
 * A "token" is each piece of command line argument that is mostly
 * separated by " ".
 *
 * Each token has a type. The type is a useful information for the
 * grammar parser, which will use this lexer.
 *
 * Before going into the details, we assume that a set of flags are
 * pre-registered, and the user may still give unregisterred flags.
 *
 * Note that the purpose of this lexer/parser is to separate cvd
 * client specific arguments and the "subcmd" from the rest. So,
 * "registered" arguments would be the cvd client specific arguments.
 * The unregisterred arguments would be for the sub tool.
 *
 * Also, in terms of lexing, boolean flags are different from other
 * value-taking flags. A boolean flag --foo could be --nofoo.
 *
 * 1. kKnownValueFlag
 *    --foo, -foo that may take a non-boolean value
 * 2. kKnownFlagAndValue
 *    --foo=value, -foo=value, which does not take more values
 * 3. kKnownBoolFlag
 *    --daemon, -daemon, etc, which may take a boolean arg
 * 4. kKnownBoolNoFlag
 *    --nodaemon, -nodaemon, etc, which does not take another argument.
 * 5. kUnknownFlag
 *    -anything_else or --anything_else
 *    --anything_else=any_value, etc
 *    Note that if we don't know the type of the flag, we will have to forward
 *    the entire thing to the subcmd as is.
 * 6. kPositional
 *    mostly without leading "-" or "--"
 * 7. kDoubleDash
 *    A literally "--"
 *    cvd and its subtools as of not are not really using that.
 *    However, it might be useful in the future for any subtool of cvd, so
 *    we allow "--" in the subcmd arguments only in the parser level.
 *    In the lexer level, we simply returns kDoubleDash token.
 * 8. kError
 *    The rest.
 *
 */
enum class ArgType : int {
  kKnownValueFlag,
  kKnownFlagAndValue,
  kKnownBoolFlag,
  kKnownBoolNoFlag,
  kUnknownFlag,
  kPositional,
  kDoubleDash,
  kError
};

class ArgToken {
 public:
  ArgToken() = delete;
  ArgToken(const ArgType arg_type, const std::string& token)
      : type_(arg_type), token_(token) {}
  ArgToken(const ArgToken& src) = default;
  ArgToken(ArgToken&& src) = default;
  ArgToken& operator=(const ArgToken& src) {
    type_ = src.type_;
    token_ = src.token_;
    return *this;
  }
  ArgToken& operator=(ArgToken&& src) {
    type_ = std::move(src.type_);
    token_ = std::move(src.token_);
    return *this;
  }

  auto Type() const { return type_; }
  const auto& Token() const { return token_; }
  bool operator==(const ArgToken& dst) const {
    return Type() == dst.Type() && Token() == dst.Token();
  }

 private:
  ArgType type_;
  std::string token_;
};

class ArgumentsLexer {
  friend class ArgumentsLexerBuilder;
  using CvdProtobufArg = google::protobuf::RepeatedPtrField<std::string>;

 public:
  Result<std::vector<ArgToken>> Tokenize(const std::vector<std::string>& args);
  Result<std::vector<ArgToken>> Tokenize(const CvdProtobufArg& args);
  Result<std::vector<ArgToken>> Tokenize(const std::string& args,
                                         const std::string delim = " ");

 private:
  // Lexer factory function will internally generate this,
  // and give it to ArgumentsLexer.
  struct FlagPatterns {
    /* represents flags that takes values
     * e.g. -device_name, --device_name (which may take an additional
     * positional arg, or use its default value.)
     *
     * With the given example, this set shall be:
     *  {"-device_name", "--device_name"}
     */
    std::unordered_set<std::string> value_patterns;
    /* boolean flags
     * e.g. --daemon, --nodaemon
     *
     * With the given example, this set shall be:
     *  {"-daemon", "--daemon"}
     */
    std::unordered_set<std::string> bool_patterns;
    // e.g. {"-nodaemon", "--nodaemon"}
    std::unordered_set<std::string> bool_no_patterns;
  };
  ArgumentsLexer(FlagPatterns&& flag_patterns);

  // preprocess boolean flags:
  //  e.g. --help=yes --> --help
  //       --help=faLSe --> --nohelp
  Result<std::vector<std::string>> Preprocess(
      const std::vector<std::string>& args);
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
  std::unordered_set<std::string> valid_bool_values_in_lower_cases_;
  FlagPatterns flag_patterns_;
};

// input to the lexer factory function
struct LexerFlagsSpecification {
  std::unordered_set<std::string> known_boolean_flags;
  std::unordered_set<std::string> known_value_flags;
};

/*
 * At the top level, there are only two tokens: flag and positional tokens.
 *
 * A flag token starts with "-" or "--" followed by one or more non "-" letters.
 * A positional token starts with any character other than "-".
 *
 * Between flag tokens, there are "known" and "unknown" flag tokens.
 *
 */
class ArgumentsLexerBuilder {
  using FlagPatterns = ArgumentsLexer::FlagPatterns;

 public:
  static Result<std::unique_ptr<ArgumentsLexer>> Build(
      const LexerFlagsSpecification& known_flags);

 private:
  static Result<FlagPatterns> GenerateFlagPatterns(
      const LexerFlagsSpecification& known_flags);
};

}  // namespace selector
}  // namespace cuttlefish
