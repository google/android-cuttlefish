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

#include "cuttlefish/pretty/container.h"

#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "fmt/format.h"
#include "gtest/gtest.h"

namespace cuttlefish {
namespace {

void ExpectFormatsTo(const PrettyContainerType& ps,
                     const std::string_view expected) {
  const std::string_view trimmed = absl::StripAsciiWhitespace(expected);

  EXPECT_EQ(absl::StrCat(ps), trimmed);
  EXPECT_EQ(fmt::format("{}", ps), trimmed);

  std::stringstream sstream;
  sstream << ps;
  EXPECT_EQ(sstream.str(), trimmed);
}

}  // namespace

TEST(PrettyContainer, Empty) {
  ExpectFormatsTo(PrettyContainer(std::vector<int>{}), "{}");
}

TEST(PrettyContainer, OneMember) {
  ExpectFormatsTo(PrettyContainer(std::vector<int>{1}), R"(
{
  1
}
)");
}

TEST(PrettyContainer, StringMember) {
  ExpectFormatsTo(PrettyContainer(std::vector<std::string_view>{"abc"}), R"(
{
  "abc"
}
)");
}

TEST(PrettyContainer, TwoMembers) {
  ExpectFormatsTo(PrettyContainer(std::vector<int>{1, 2}), R"(
{
  1,
  2
}
)");
}

TEST(PrettyContainer, MembersWithNewlines) {
  ExpectFormatsTo(PrettyContainer(std::vector<std::string_view>{"abc\ndef"}),
                  R"(
{
  "abc
  def"
}
)");
}

TEST(PrettyContainer, NestedMember) {
  std::vector<std::vector<int>> container = {{1, 2}, {3, 4}};
  ExpectFormatsTo(PrettyContainer(container, PrettyContainer<std::vector<int>>),
                  R"(
{
  {
    1,
    2
  },
  {
    3,
    4
  }
}
)");
}

}  // namespace cuttlefish
