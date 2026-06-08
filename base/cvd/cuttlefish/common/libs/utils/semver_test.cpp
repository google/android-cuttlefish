/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/semver.h"

#include <string>

#include "gtest/gtest.h"

namespace cuttlefish {

TEST(SemVer, ParseBasic) {
  auto result = ParseSemVer("1.2.3");
  ASSERT_TRUE(result.ok()) << result.error().Trace();
  EXPECT_EQ(result->major, 1);
  EXPECT_EQ(result->minor, 2);
  EXPECT_EQ(result->patch, 3);
  EXPECT_EQ(result->prerelease, "");
  EXPECT_EQ(result->build_metadata, "");
}

TEST(SemVer, ParseWithPrereleaseAndMetadata) {
  auto result = ParseSemVer("1.2.3-alpha.1+build.123");
  ASSERT_TRUE(result.ok()) << result.error().Trace();
  EXPECT_EQ(result->major, 1);
  EXPECT_EQ(result->minor, 2);
  EXPECT_EQ(result->patch, 3);
  EXPECT_EQ(result->prerelease, "alpha.1");
  EXPECT_EQ(result->build_metadata, "build.123");
}

TEST(SemVer, ParseInvalid) {
  auto result = ParseSemVer("asdfsf");
  EXPECT_FALSE(result.ok());
}

}  // namespace cuttlefish
