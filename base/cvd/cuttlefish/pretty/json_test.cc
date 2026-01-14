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

#include "cuttlefish/pretty/json.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "json/value.h"

namespace cuttlefish {

TEST(PrettyJson, Structured) {
  Json::Value structured;
  structured["array"] = Json::Value(Json::arrayValue);
  structured["array"].append(123);
  structured["array"].append(456);
  structured["array"].append(789);
  structured["string"] = "value";

  std::string formatted = Pretty(structured);

  // Assert there is whitespace (implying formatting) without depending on the
  // exact formatting output.
  EXPECT_TRUE(absl::StrContains(formatted, "\n"));
  EXPECT_TRUE(absl::StrContains(formatted, "  "));
  EXPECT_TRUE(absl::StrContains(formatted, "123"));
  EXPECT_TRUE(absl::StrContains(formatted, "456"));
  EXPECT_TRUE(absl::StrContains(formatted, "789"));
  EXPECT_TRUE(absl::StrContains(formatted, "\"value\""));
}

}  // namespace cuttlefish
