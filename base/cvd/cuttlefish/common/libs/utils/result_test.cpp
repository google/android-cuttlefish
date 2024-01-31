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

#include "common/libs/utils/result.h"

#include <string>
#include <type_traits>

#include <android-base/expected.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/libs/utils/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::HasSubstr;
using ::testing::StrEq;

}  // namespace

TEST(ResultTest, ExpectBoolGoodNoMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT(true);
    return "okay";
  }();
  EXPECT_THAT(result, IsOkAndValue(StrEq("okay")));
}

TEST(ResultTest, ExpectBoolGoodWithMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT(true, "Failed");
    return "okay";
  }();
  EXPECT_THAT(result, IsOkAndValue(StrEq("okay")));
}

TEST(ResultTest, ExpectBoolBadNoMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT(false);
    return "okay";
  }();
  EXPECT_THAT(result, IsError());
}

TEST(ResultTest, ExpectBoolBadWithMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT(false, "ExpectBoolBadWithMessage message");
    return "okay";
  }();
  EXPECT_THAT(result,
              IsErrorAndMessage(HasSubstr("ExpectBoolBadWithMessage message")));
}

TEST(ResultTest, ExpectWithResultGoodNoMessage) {
  const auto result = []() -> Result<std::string> {
    const auto inner_result = []() -> Result<std::string> {
      CF_EXPECT(true);
      return "inner okay";
    };
    CF_EXPECT(inner_result());
    return "outer okay";
  }();
  EXPECT_THAT(result, IsOkAndValue(StrEq("outer okay")));
}

TEST(ResultTest, ExpectWithResultGoodWithMessage) {
  const auto result = []() -> Result<std::string> {
    const auto inner_result = []() -> Result<std::string> {
      CF_EXPECT(true);
      return "inner okay";
    };
    CF_EXPECT(inner_result(), "Failed inner result.");
    return "outer okay";
  }();
  EXPECT_THAT(result, IsOkAndValue(StrEq("outer okay")));
}

TEST(ResultTest, ExpectWithResultBadNoMessage) {
  const auto result = []() -> Result<std::string> {
    const auto inner_result = []() -> Result<std::string> {
      CF_EXPECT(false, "inner bad");
      return "inner okay";
    };
    CF_EXPECT(inner_result());
    return "okay";
  }();
  EXPECT_THAT(result, IsError());
}

TEST(ResultTest, ExpectWithResultBadWithMessage) {
  const auto result = []() -> Result<std::string> {
    const auto inner_result = []() -> Result<std::string> {
      CF_EXPECT(false, "inner bad");
      return "inner okay";
    };
    CF_EXPECT(inner_result(), "ExpectWithResultBadWithMessage message");
    return "okay";
  }();
  EXPECT_THAT(result, IsErrorAndMessage(
                          HasSubstr("ExpectWithResultBadWithMessage message")));
}

TEST(ResultTest, ExpectEqGoodNoMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT_EQ(1, 1);
    return "okay";
  }();
  EXPECT_THAT(result, IsOkAndValue(StrEq("okay")));
}

TEST(ResultTest, ExpectEqGoodWithMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT_EQ(1, 1, "Failed comparison");
    return "okay";
  }();
  EXPECT_THAT(result, IsOkAndValue(StrEq("okay")));
}

TEST(ResultTest, ExpectEqBadNoMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT_EQ(1, 2);
    return "okay";
  }();
  EXPECT_THAT(result, IsError());
}

TEST(ResultTest, ExpectEqBadWithMessage) {
  const auto result = []() -> Result<std::string> {
    CF_EXPECT_EQ(1, 2, "ExpectEqBadWithMessage message");
    return "okay";
  }();
  EXPECT_THAT(result,
              IsErrorAndMessage(HasSubstr("ExpectEqBadWithMessage message")));
}

}  // namespace cuttlefish
