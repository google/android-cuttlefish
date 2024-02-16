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

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "host/commands/cvd/selector/arguments_lexer.h"

namespace cuttlefish {
namespace selector {

using Tokens = std::vector<ArgToken>;

struct LexerInputOutput {
  LexerFlagsSpecification known_flags_;
  std::string lex_input_;
  std::optional<Tokens> expected_tokens_;
};

class LexerTestBase : public testing::TestWithParam<LexerInputOutput> {
 protected:
  LexerTestBase();
  void Init();

  LexerFlagsSpecification known_flags_;
  std::string lex_input_;
  std::optional<Tokens> expected_tokens_;
};

class EmptyArgsLexTest : public LexerTestBase {};
class NonBooleanArgsTest : public LexerTestBase {};
class BooleanArgsTest : public LexerTestBase {};
class BothArgsTest : public LexerTestBase {};

class BooleanBadArgsTest : public LexerTestBase {};

}  // namespace selector
}  // namespace cuttlefish
