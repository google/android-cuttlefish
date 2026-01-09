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

#include "cuttlefish/pretty/struct.h"

#include <string_view>

#include "absl/strings/ascii.h"
#include "fmt/format.h"
#include "gtest/gtest.h"

namespace cuttlefish {
namespace {

void ExpectFormatsTo(const PrettyStruct& ps, const std::string_view expected) {
  const std::string_view trimmed = absl::StripAsciiWhitespace(expected);

  EXPECT_EQ(absl::StrCat(ps), trimmed);
  EXPECT_EQ(fmt::format("{}", ps), trimmed);

  std::stringstream sstream;
  sstream << ps;
  EXPECT_EQ(sstream.str(), trimmed);
}

}  // namespace

TEST(PrettyStruct, Empty) {
  ExpectFormatsTo(PrettyStruct("Empty"), "Empty {}");
}

TEST(PrettyStruct, OneMember) {
  ExpectFormatsTo(PrettyStruct("Pretty").Member("member", 5), R"(
Pretty {
  member: 5
}
)");
}

TEST(PrettyStruct, StringMember) {
  ExpectFormatsTo(PrettyStruct("Pretty").Member("member", "value"), R"(
Pretty {
  member: "value"
}
)");
}

TEST(PrettyStruct, TwoMembers) {
  ExpectFormatsTo(
      PrettyStruct("Pretty").Member("member_a", 5).Member("member_b", 6), R"(
Pretty {
  member_a: 5,
  member_b: 6
}
)");
}

TEST(PrettyStruct, NestedMembers) {
  const PrettyStruct inner =
      PrettyStruct("Inner").Member("i1", 1).Member("i2", 2);
  ExpectFormatsTo(PrettyStruct("Outer").Member("o1", inner).Member("o2", inner),
                  R"(
Outer {
  o1: Inner {
    i1: 1,
    i2: 2
  },
  o2: Inner {
    i1: 1,
    i2: 2
  }
}
)");
}

}  // namespace cuttlefish
