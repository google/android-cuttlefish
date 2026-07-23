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

#include <type_traits>

#include "gmock/gmock.h"
#include "tl/expected.hpp"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

MATCHER(IsOk, "an ok result") {
  auto& result = arg;
  if (!result.has_value()) {
    *result_listener << "which is an error result with trace: "
                     << result.error().Trace();
    return false;
  }
  return true;
}

MATCHER(IsError, "an error result") {
  auto& result = arg;
  if (result.has_value()) {
    *result_listener << "which is an ok result";
    return false;
  }
  return true;
}

MATCHER_P(IsOkAndValue, result_value_matcher, "") {
  auto get_value = [](const auto& res) -> auto { return res.value(); };
  return ExplainMatchResult(
      ::testing::AllOf(IsOk(), ::testing::ResultOf("value", get_value,
                                                   result_value_matcher)),
      arg, result_listener);
}

MATCHER_P(IsErrorAndMessage, message_matcher, "") {
  auto get_error = [](const auto& res) -> auto { return res.error(); };
  return ExplainMatchResult(
      ::testing::AllOf(
          IsError(),
          ::testing::ResultOf(
              "error", get_error,
              ::testing::Property(&StackTraceError::Message, message_matcher))),
      arg, result_listener);
}

}  // namespace cuttlefish
