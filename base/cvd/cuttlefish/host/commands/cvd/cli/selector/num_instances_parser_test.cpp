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

#include "cuttlefish/host/commands/cvd/cli/selector/num_instances_parser.h"

#include "gtest/gtest.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace selector {

struct NumInstancesParserTestParam {
  SelectorOptions selector_options;
  cvd_common::Args args;
  bool should_fail = false;
  size_t expected_num_instances = 0;
  std::vector<unsigned> expected_instance_nums = {};

  NumInstancesParserTestParam& WithArgs(cvd_common::Args args) {
    this->args = std::move(args);
    return *this;
  }
  NumInstancesParserTestParam& WithNames(std::vector<std::string> names) {
    selector_options.instance_names = std::move(names);
    return *this;
  }
  NumInstancesParserTestParam& ExpectFailure() {
    should_fail = true;
    return *this;
  }
  NumInstancesParserTestParam& ExpectNum(size_t num) {
    expected_num_instances = num;
    return *this;
  }
  NumInstancesParserTestParam& ExpectIds(std::vector<unsigned> ids) {
    expected_num_instances = ids.size();
    expected_instance_nums = ids;
    return *this;
  }
};

class NumInstancesParserTest
    : public testing::TestWithParam<NumInstancesParserTestParam> {};

TEST_P(NumInstancesParserTest, ParseFlags) {
  NumInstancesParser parser;
  cvd_common::Args args = GetParam().args;
  Result<void> parse_result =
      ConsumeFlags(parser.Flags(GetParam().selector_options), args);
  if (GetParam().should_fail) {
    ASSERT_THAT(parse_result, IsError());
    return;
  } else {
    ASSERT_THAT(parse_result, IsOk());
  }
  ASSERT_EQ(GetParam().expected_num_instances, parser.NumInstances());
  ASSERT_EQ(GetParam().expected_instance_nums, parser.InstanceIds());
}

INSTANTIATE_TEST_SUITE_P(
    ParseNumInstances, NumInstancesParserTest,
    testing::Values(NumInstancesParserTestParam().WithArgs({}).ExpectNum(1),
                    NumInstancesParserTestParam()
                        .WithArgs({"--num_instances", "2"})
                        .ExpectNum(2)));

INSTANTIATE_TEST_SUITE_P(ParseInstanceNums, NumInstancesParserTest,
                         testing::Values(NumInstancesParserTestParam()
                                             .WithArgs({"--instance_nums", "2"})
                                             .ExpectIds({2}),
                                         NumInstancesParserTestParam()
                                             .WithArgs({"--instance_nums",
                                                        "2,5,6"})
                                             .ExpectIds({2, 5, 6})));

INSTANTIATE_TEST_SUITE_P(ParseInstanceName, NumInstancesParserTest,
                         testing::Values(NumInstancesParserTestParam()
                                             .WithNames({"c-1", "c-3", "c-5"})
                                             .ExpectNum(3)));

INSTANTIATE_TEST_SUITE_P(
    ParseCombined, NumInstancesParserTest,
    testing::Values(
        NumInstancesParserTestParam()
            .WithArgs({"--instance_nums", "2,5,7", "--num_instances", "3"})
            .ExpectIds({2, 5, 7}),
        NumInstancesParserTestParam()
            .WithNames({"c-1", "c-2", "c-3"})
            .WithArgs({"--instance_nums", "2,3,5", "--num_instances", "3"})
            .ExpectIds({2, 3, 5})));

INSTANTIATE_TEST_SUITE_P(
    ParseBaseInstanceNum, NumInstancesParserTest,
    testing::Values(NumInstancesParserTestParam()
                        .WithArgs({"--base_instance_num", "6"})
                        .ExpectIds({6}),
                    NumInstancesParserTestParam()
                        .WithArgs({"--base_instance_num", "5",
                                   "--num_instances", "3"})
                        .ExpectIds({5, 6, 7}),
                    NumInstancesParserTestParam()
                        .WithNames({"c-1", "c-2", "c-3"})
                        .WithArgs({"--base_instance_num", "4"})
                        .ExpectIds({4, 5, 6})));

INSTANTIATE_TEST_SUITE_P(
    ParseInvalid, NumInstancesParserTest,
    testing::Values(
        NumInstancesParserTestParam()
            .WithArgs({"--base_instance_num", "6", "--instance_nums", "6,7"})
            .ExpectFailure(),
        NumInstancesParserTestParam()
            .WithArgs({"--instance_nums", "5", "--num_instances", "3"})
            .ExpectFailure(),
        NumInstancesParserTestParam()
            .WithNames({"c-1", "c-2", "c-3"})
            .WithArgs({"--num_instances", "4"})
            .ExpectFailure(),
        NumInstancesParserTestParam()
            .WithNames({"c-1", "c-2", "c-3"})
            .WithArgs({"--instance_nums", "4,8"})
            .ExpectFailure()));

}  // namespace selector
}  // namespace cuttlefish
