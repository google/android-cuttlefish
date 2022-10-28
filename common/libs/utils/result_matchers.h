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

#include <android-base/expected.h>
#include <gmock/gmock.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {

MATCHER(IsOk, "an ok result") {
  auto& result = arg;
  if (!result.ok()) {
    *result_listener << "which is an error result with trace: "
                     << result.error().Message();
    return false;
  }
  return true;
}

MATCHER(IsError, "an error result") {
  auto& result = arg;
  if (result.ok()) {
    *result_listener << "which is an ok result";
    return false;
  }
  return true;
}

MATCHER_P(IsOkAndValue, result_value_matcher, "") {
  auto& result = arg;
  using ResultType = std::decay_t<decltype(result)>;
  return ExplainMatchResult(
      ::testing::AllOf(IsOk(), ::testing::Property("value", &ResultType::value,
                                                   result_value_matcher)),
      result, result_listener);
}

MATCHER_P(IsErrorAndMessage, message_matcher, "") {
  auto& result = arg;
  using ResultType = std::decay_t<decltype(result)>;
  return ExplainMatchResult(
      ::testing::AllOf(
          IsError(),
          ::testing::Property(
              "error", &ResultType::error,
              ::testing::Property(&StackTraceError::Message, message_matcher))),
      result, result_listener);
}

}  // namespace cuttlefish
