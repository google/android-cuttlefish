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

#include <common/libs/utils/flag_parser.h>

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace cuttlefish {

TEST(FlagParser, StringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_TRUE(flag.Parse({"-myflag=a"}));
  ASSERT_EQ(value, "a");
  ASSERT_TRUE(flag.Parse({"--myflag=b"}));
  ASSERT_EQ(value, "b");
  ASSERT_TRUE(flag.Parse({"-myflag", "c"}));
  ASSERT_EQ(value, "c");
  ASSERT_TRUE(flag.Parse({"--myflag", "d"}));
  ASSERT_EQ(value, "d");
  ASSERT_TRUE(flag.Parse({"--myflag="}));
  ASSERT_EQ(value, "");
}

TEST(FlagParser, RepeatedStringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_TRUE(flag.Parse({"-myflag=a", "--myflag", "b"}));
  ASSERT_EQ(value, "b");
}

TEST(FlagParser, RepeatedListFlag) {
  std::vector<std::string> elems;
  auto flag = GflagsCompatFlag("myflag");
  flag.Setter([&elems](const FlagMatch& match) {
    elems.push_back(match.value);
    return true;
  });
  ASSERT_TRUE(flag.Parse({"-myflag=a", "--myflag", "b"}));
  ASSERT_EQ(elems, (std::vector<std::string>{"a", "b"}));
}

TEST(FlagParser, FlagRemoval) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  std::vector<std::string> flags = {"-myflag=a", "-otherflag=c"};
  ASSERT_TRUE(flag.Parse(flags));
  ASSERT_EQ(value, "a");
  ASSERT_EQ(flags, std::vector<std::string>{"-otherflag=c"});
  flags = {"-otherflag=a", "-myflag=c"};
  ASSERT_TRUE(flag.Parse(flags));
  ASSERT_EQ(value, "c");
  ASSERT_EQ(flags, std::vector<std::string>{"-otherflag=a"});
}

TEST(FlagParser, IntFlag) {
  std::int32_t value = 0;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_TRUE(flag.Parse({"-myflag=5"}));
  ASSERT_EQ(value, 5);
  ASSERT_TRUE(flag.Parse({"--myflag=6"}));
  ASSERT_EQ(value, 6);
  ASSERT_TRUE(flag.Parse({"-myflag", "7"}));
  ASSERT_EQ(value, 7);
  ASSERT_TRUE(flag.Parse({"--myflag", "8"}));
  ASSERT_EQ(value, 8);
}

TEST(FlagParser, BoolFlag) {
  bool value = false;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_TRUE(flag.Parse({"-myflag"}));
  ASSERT_TRUE(value);

  value = false;
  ASSERT_TRUE(flag.Parse({"--myflag"}));
  ASSERT_TRUE(value);

  value = false;
  ASSERT_TRUE(flag.Parse({"-myflag=true"}));
  ASSERT_TRUE(value);

  value = false;
  ASSERT_TRUE(flag.Parse({"--myflag=true"}));
  ASSERT_TRUE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"-nomyflag"}));
  ASSERT_FALSE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"--nomyflag"}));
  ASSERT_FALSE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"-myflag=false"}));
  ASSERT_FALSE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"--myflag=false"}));
  ASSERT_FALSE(value);

  ASSERT_FALSE(flag.Parse({"--myflag=nonsense"}));
}

TEST(FlagParser, StringIntFlag) {
  std::int32_t int_value = 0;
  std::string string_value;
  auto int_flag = GflagsCompatFlag("int", int_value);
  auto string_flag = GflagsCompatFlag("string", string_value);
  std::vector<Flag> flags = {int_flag, string_flag};
  ASSERT_TRUE(ParseFlags(flags, {"-int=5", "-string=a"}));
  ASSERT_EQ(int_value, 5);
  ASSERT_EQ(string_value, "a");
  ASSERT_TRUE(ParseFlags(flags, {"--int=6", "--string=b"}));
  ASSERT_EQ(int_value, 6);
  ASSERT_EQ(string_value, "b");
  ASSERT_TRUE(ParseFlags(flags, {"-int", "7", "-string", "c"}));
  ASSERT_EQ(int_value, 7);
  ASSERT_EQ(string_value, "c");
  ASSERT_TRUE(ParseFlags(flags, {"--int", "8", "--string", "d"}));
  ASSERT_EQ(int_value, 8);
  ASSERT_EQ(string_value, "d");
}

TEST(FlagParser, InvalidStringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_FALSE(flag.Parse({"-myflag"}));
  ASSERT_FALSE(flag.Parse({"--myflag"}));
}

TEST(FlagParser, InvalidIntFlag) {
  int value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_FALSE(flag.Parse({"-myflag"}));
  ASSERT_FALSE(flag.Parse({"--myflag"}));
  ASSERT_FALSE(flag.Parse({"-myflag=abc"}));
  ASSERT_FALSE(flag.Parse({"--myflag=def"}));
  ASSERT_FALSE(flag.Parse({"-myflag", "abc"}));
  ASSERT_FALSE(flag.Parse({"--myflag", "def"}));
}

TEST(FlagParser, InvalidFlagGuard) {
  auto flag = InvalidFlagGuard();
  ASSERT_TRUE(flag.Parse({}));
  ASSERT_TRUE(flag.Parse({"positional"}));
  ASSERT_TRUE(flag.Parse({"positional", "positional2"}));
  ASSERT_FALSE(flag.Parse({"-flag"}));
  ASSERT_FALSE(flag.Parse({"-"}));
}

TEST(FlagParser, UnexpectedArgumentGuard) {
  auto flag = UnexpectedArgumentGuard();
  ASSERT_TRUE(flag.Parse({}));
  ASSERT_FALSE(flag.Parse({"positional"}));
  ASSERT_FALSE(flag.Parse({"positional", "positional2"}));
  ASSERT_FALSE(flag.Parse({"-flag"}));
  ASSERT_FALSE(flag.Parse({"-"}));
}

}  // namespace cuttlefish
