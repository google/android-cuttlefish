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

#include <gtest/gtest.h>

#include "host/commands/cvd/selector/arguments_lexer.h"
#include "host/commands/cvd/unittests/selector/client_lexer_helper.h"

namespace cuttlefish {
namespace selector {
namespace {

const LexerFlagsSpecification empty_known_flags;
const LexerFlagsSpecification boolean_known_flags{
    .known_boolean_flags = {"clean"}};
const LexerFlagsSpecification non_boolean_known_flags{
    .known_value_flags = {"group_name"}};
const LexerFlagsSpecification both_known_flags{
    .known_boolean_flags = {"clean"}, .known_value_flags = {"group_name"}};

}  // namespace

TEST_P(EmptyArgsLexTest, SuccessExpectedTest) {
  auto lexer_gen_result = ArgumentsLexerBuilder::Build(known_flags_);
  std::unique_ptr<ArgumentsLexer> lexer =
      lexer_gen_result.ok() ? std::move(*lexer_gen_result) : nullptr;
  if (!lexer) {
    GTEST_SKIP() << "Memory allocation failed but it is not in the test scope.";
  }
  auto tokenized_result = lexer->Tokenize(lex_input_);

  ASSERT_TRUE(tokenized_result.ok()) << tokenized_result.error().Trace();
  ASSERT_EQ(*tokenized_result, *expected_tokens_);
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, EmptyArgsLexTest,
    testing::Values(LexerInputOutput{.known_flags_ = empty_known_flags,
                                     .lex_input_ = "",
                                     .expected_tokens_ = Tokens{}},
                    LexerInputOutput{.known_flags_ = boolean_known_flags,
                                     .lex_input_ = "",
                                     .expected_tokens_ = Tokens{}},
                    LexerInputOutput{.known_flags_ = non_boolean_known_flags,
                                     .lex_input_ = "",
                                     .expected_tokens_ = Tokens{}},
                    LexerInputOutput{.known_flags_ = both_known_flags,
                                     .lex_input_ = "",
                                     .expected_tokens_ = Tokens{}}));

TEST_P(NonBooleanArgsTest, SuccessExpectedTest) {
  auto lexer_gen_result = ArgumentsLexerBuilder::Build(known_flags_);
  std::unique_ptr<ArgumentsLexer> lexer =
      lexer_gen_result.ok() ? std::move(*lexer_gen_result) : nullptr;
  if (!lexer) {
    GTEST_SKIP() << "Memory allocation failed but it is not in the test scope.";
  }
  auto tokenized_result = lexer->Tokenize(lex_input_);

  ASSERT_TRUE(tokenized_result.ok()) << tokenized_result.error().Trace();
  ASSERT_EQ(*tokenized_result, *expected_tokens_);
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, NonBooleanArgsTest,
    testing::Values(
        LexerInputOutput{
            .known_flags_ = non_boolean_known_flags,
            .lex_input_ = "cvd --group_name=yumi",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownFlagAndValue,
                                                "--group_name=yumi"}}},
        LexerInputOutput{
            .known_flags_ = non_boolean_known_flags,
            .lex_input_ = "cvd --group_name yumi",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownValueFlag,
                                                "--group_name"},
                                       ArgToken{ArgType::kPositional, "yumi"}}},
        LexerInputOutput{.known_flags_ = non_boolean_known_flags,
                         .lex_input_ = "cvd --group_name yumi start --daemon",
                         .expected_tokens_ = Tokens{
                             ArgToken{ArgType::kPositional, "cvd"},
                             ArgToken{ArgType::kKnownValueFlag, "--group_name"},
                             ArgToken{ArgType::kPositional, "yumi"},
                             ArgToken{ArgType::kPositional, "start"},
                             ArgToken{ArgType::kUnknownFlag, "--daemon"}}}));

TEST_P(BooleanArgsTest, SuccessExpectedTest) {
  auto lexer_gen_result = ArgumentsLexerBuilder::Build(known_flags_);
  std::unique_ptr<ArgumentsLexer> lexer =
      lexer_gen_result.ok() ? std::move(*lexer_gen_result) : nullptr;
  if (!lexer) {
    GTEST_SKIP() << "Memory allocation failed but it is not in the test scope.";
  }
  auto tokenized_result = lexer->Tokenize(lex_input_);

  ASSERT_TRUE(tokenized_result.ok()) << tokenized_result.error().Trace();
  ASSERT_EQ(*tokenized_result, *expected_tokens_);
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, BooleanArgsTest,
    testing::Values(
        LexerInputOutput{
            .known_flags_ = boolean_known_flags,
            .lex_input_ = "cvd --clean",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownBoolFlag,
                                                "--clean"}}},
        LexerInputOutput{
            .known_flags_ = boolean_known_flags,
            .lex_input_ = "cvd --clean=TrUe",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownBoolFlag,
                                                "--clean"}}},
        LexerInputOutput{
            .known_flags_ = boolean_known_flags,
            .lex_input_ = "cvd --noclean",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownBoolNoFlag,
                                                "--noclean"}}},
        LexerInputOutput{
            .known_flags_ = boolean_known_flags,
            .lex_input_ = "cvd --noclean=redundant",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownBoolNoFlag,
                                                "--noclean"}}},
        LexerInputOutput{
            .known_flags_ = boolean_known_flags,
            .lex_input_ = "cvd --clean=no --norandom=y",
            .expected_tokens_ = Tokens{
                ArgToken{ArgType::kPositional, "cvd"},
                ArgToken{ArgType::kKnownBoolNoFlag, "--noclean"},
                ArgToken{ArgType::kUnknownFlag, "--norandom=y"}}}));

TEST_P(BothArgsTest, SuccessExpectedTest) {
  auto lexer_gen_result = ArgumentsLexerBuilder::Build(known_flags_);
  std::unique_ptr<ArgumentsLexer> lexer =
      lexer_gen_result.ok() ? std::move(*lexer_gen_result) : nullptr;
  if (!lexer) {
    GTEST_SKIP() << "Memory allocation failed but it is not in the test scope.";
  }
  auto tokenized_result = lexer->Tokenize(lex_input_);

  ASSERT_TRUE(tokenized_result.ok()) << tokenized_result.error().Trace();
  ASSERT_EQ(*tokenized_result, *expected_tokens_);
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, BothArgsTest,
    testing::Values(
        LexerInputOutput{
            .known_flags_ = both_known_flags,
            .lex_input_ = "cvd --clean -group_name=yumi",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kKnownBoolFlag,
                                                "--clean"},
                                       ArgToken{ArgType::kKnownFlagAndValue,
                                                "-group_name=yumi"}}},
        LexerInputOutput{
            .known_flags_ = both_known_flags,
            .lex_input_ = "cvd --group_name -noclean",
            .expected_tokens_ = Tokens{
                ArgToken{ArgType::kPositional, "cvd"},
                ArgToken{ArgType::kKnownValueFlag, "--group_name"},
                ArgToken{ArgType::kKnownBoolNoFlag, "-noclean"}}}));

TEST_P(BooleanBadArgsTest, FailureExpectedTest) {
  auto lexer_gen_result = ArgumentsLexerBuilder::Build(known_flags_);
  std::unique_ptr<ArgumentsLexer> lexer =
      lexer_gen_result.ok() ? std::move(*lexer_gen_result) : nullptr;
  if (!lexer) {
    GTEST_SKIP() << "Memory allocation failed but it is not in the test scope.";
  }
  auto tokenized_result = lexer->Tokenize(lex_input_);

  if (!expected_tokens_) {
    ASSERT_FALSE(tokenized_result.ok())
        << "Lexing " << lex_input_ << " should have failed.";
    return;
  }
  ASSERT_TRUE(tokenized_result.ok()) << tokenized_result.error().Trace();
  ASSERT_EQ(*tokenized_result, *expected_tokens_);
}

INSTANTIATE_TEST_SUITE_P(
    ClientSpecificOptionParser, BooleanBadArgsTest,
    testing::Values(
        LexerInputOutput{
            .known_flags_ = boolean_known_flags,
            .lex_input_ = "cvd --yesclean",
            .expected_tokens_ = Tokens{ArgToken{ArgType::kPositional, "cvd"},
                                       ArgToken{ArgType::kUnknownFlag,
                                                "--yesclean"}}},
        LexerInputOutput{.known_flags_ = boolean_known_flags,
                         .lex_input_ = "cvd --clean=Hello",
                         .expected_tokens_ = std::nullopt},
        LexerInputOutput{.known_flags_ = boolean_known_flags,
                         .lex_input_ = "cvd --clean false",
                         .expected_tokens_ = Tokens{
                             ArgToken{ArgType::kPositional, "cvd"},
                             ArgToken{ArgType::kKnownBoolFlag, "--clean"},
                             ArgToken{ArgType::kPositional, "false"}}}));

}  // namespace selector
}  // namespace cuttlefish
