//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/parse_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(ParseTimeTest, WithMilliseconds) {
  EXPECT_THAT(ParseTime("2026-08-03T11:56:20.100Z"), IsOk());
}

}  // namespace
}  // namespace cuttlefish
