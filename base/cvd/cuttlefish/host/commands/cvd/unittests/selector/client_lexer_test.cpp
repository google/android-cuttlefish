//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>

#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"
#include "host/commands/cvd/cli/selector/arguments_lexer.h"
#include "host/commands/cvd/unittests/selector/client_lexer_helper.h"

namespace cuttlefish {
namespace selector {
namespace {

Result<std::vector<ArgToken>> Tokenize(const std::string& args) {
  auto args_vec = android::base::Tokenize(args, " ");
  return CF_EXPECT(TokenizeArguments(args_vec));
}

}  // namespace

TEST_P(EmptyArgsLexTest, SuccessExpectedTest) {
  EXPECT_THAT(Tokenize(lex_input_), IsOkAndValue(*expected_tokens_));
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, EmptyArgsLexTest,
    testing::Values(
        LexerInputOutput{.lex_input_ = "", .expected_tokens_ = Tokens{}},
        LexerInputOutput{.lex_input_ = "", .expected_tokens_ = Tokens{}},
        LexerInputOutput{.lex_input_ = "", .expected_tokens_ = Tokens{}},
        LexerInputOutput{.lex_input_ = "", .expected_tokens_ = Tokens{}}));

TEST_P(NonBooleanArgsTest, SuccessExpectedTest) {
  EXPECT_THAT(Tokenize(lex_input_), IsOkAndValue(*expected_tokens_));
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, NonBooleanArgsTest,
    testing::Values(
        LexerInputOutput{
            .lex_input_ = "cvd --group_name=yumi",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownFlagAndValue,
                                                "--group_name=yumi"}}},
        LexerInputOutput{
            .lex_input_ = "cvd --group_name yumi",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownValueFlag,
                                                "--group_name"},
                                       ArgToken{ArgType::kPositional, "yumi"}}},
        LexerInputOutput{.lex_input_ = "cvd --group_name yumi start --daemon",
                         .expected_tokens_ = Tokens{
                             ArgToken{ArgType::kPositional, "cvd"},
                             ArgToken{ArgType::kKnownValueFlag, "--group_name"},
                             ArgToken{ArgType::kPositional, "yumi"},
                             ArgToken{ArgType::kPositional, "start"},
                             ArgToken{ArgType::kUnknownFlag, "--daemon"}}}));

TEST_P(BooleanBadArgsTest, FailureExpectedTest) {
  auto tokenized_result = Tokenize(lex_input_);

  if (!expected_tokens_) {
    ASSERT_FALSE(tokenized_result.ok())
        << "Lexing " << lex_input_ << " should have failed.";
    return;
  }
  EXPECT_THAT(tokenized_result, IsOkAndValue(*expected_tokens_));
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, BooleanBadArgsTest,
    testing::Values(
        LexerInputOutput{
            .lex_input_ = "cvd --yesclean",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kUnknownFlag,
                                                "--yesclean"}}},
        LexerInputOutput{
            .lex_input_ = "cvd --clean",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kUnknownFlag,
                                                "--clean"}}},
        LexerInputOutput{.lex_input_ = "cvd --clean false",
                         .expected_tokens_ = Tokens{
                             ArgToken{ArgType::kPositional, "cvd"},
                             ArgToken{ArgType::kUnknownFlag, "--clean"},
                             ArgToken{ArgType::kPositional, "false"}}}));

}  // namespace selector
}  // namespace cuttlefish
