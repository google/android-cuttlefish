
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

#include "cuttlefish/flag_parser/flag.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

TEST(FlagParser, DuplicateAlias) {
  ASSERT_DEATH(
      { Flag::StringFlag("flag").Alias("flag"); },
      "Duplicate flag alias: flag");
  ASSERT_DEATH(
      { Flag::StringFlag("flag").Alias("foo").Alias("foo"); },
      "Duplicate flag alias: foo");
}

TEST(FlagParser, RepeatedListFlag) {
  std::vector<std::string> elems;
  auto flag = Flag::StringFlag("myflag");
  flag.Setter([&elems](std::string_view arg) -> Result<void> {
    elems.emplace_back(arg);
    return {};
  });
  ASSERT_THAT(ConsumeFlags({flag}, {"-myflag=a", "--myflag", "b"}), IsOk());
  ASSERT_EQ(elems, (std::vector<std::string>{"a", "b"}));
}

TEST(FlagParser, UnexpectedArgumentGuard) {
  auto consume_check_unexpected = [](std::vector<std::string> args) {
    return ConsumeFlags({}, std::move(args),
                        {.fail_on_unexpected_argument = true});
  };
  ASSERT_THAT(consume_check_unexpected({}), IsOk());
  EXPECT_THAT(consume_check_unexpected({"positional"}), IsError());
  EXPECT_THAT(consume_check_unexpected({"positional", "positional2"}),
              IsError());
  EXPECT_THAT(consume_check_unexpected({"-flag"}), IsError());
  EXPECT_THAT(consume_check_unexpected({"-"}), IsError());
}

TEST(FlagParser, EndOfOptionMark) {
  std::vector<std::string> args{"-flag", "--", "-invalid_flag"};
  bool flag = false;
  std::vector<Flag> flags{GflagsCompatFlag("flag", flag)};

  EXPECT_THAT(ConsumeFlags(flags, args,
                           {
                               .fail_on_unexpected_argument = true,
                           }),
              IsError());
  EXPECT_EQ(args, std::vector<std::string>({"--", "-invalid_flag"}));
  EXPECT_THAT(ConsumeFlags(flags, args,
                           {
                               .stop_at_double_dashes = true,
                               .fail_on_unexpected_argument = true,
                           }),
              IsOk());
  ASSERT_TRUE(flag);
}

TEST(FlagParser, ValueNameHint_Customized) {
  auto flag = Flag::StringFlag("myflag").ValueNameHint("myvalue");
  EXPECT_EQ(flag.Synopsis(), "--myflag=myvalue");
}

TEST(FlagParser, ValueNameHint_CustomizedWithAlias) {
  auto flag = Flag::StringFlag("myflag").Alias("mf").ValueNameHint("myvalue");
  EXPECT_EQ(flag.Synopsis(), "--myflag=myvalue, --mf=myvalue");
}

TEST(FlagParser, ValueNameHint_DefaultDerived) {
  auto flag = Flag::StringFlag("myflag");
  EXPECT_EQ(flag.Synopsis(), "--myflag=MYFLAG");
}

TEST(FlagParser, ValueNameHint_DefaultDerivedDashed) {
  auto flag = Flag::StringFlag("verbosity-level");
  EXPECT_EQ(flag.Synopsis(), "--verbosity-level=LEVEL");
}

TEST(FlagParser, ValueNameHint_DefaultDerivedSnaked) {
  auto flag = Flag::StringFlag("verbosity_level");
  EXPECT_EQ(flag.Synopsis(), "--verbosity_level=LEVEL");
}

}  // namespace cuttlefish
