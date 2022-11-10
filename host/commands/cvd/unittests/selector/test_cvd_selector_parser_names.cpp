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

#include "host/commands/cvd/unittests/selector/selector_parser_names_test_helper.h"

namespace cuttlefish {
namespace selector {

TEST_P(ValidNamesTest, ValidInputs) { ASSERT_TRUE(parser_); }

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */
TEST_P(ValidNamesTest, FieldsNoSubstring) {
  if (!parser_) {
    /*
     * We aren't testing whether or not parsing is working.
     * That's tested in ValidInputs tests. We test fields.
     */
    GTEST_SKIP() << "Parsing failed, which must be tested in ValidInputs Test";
  }

  ASSERT_EQ(parser_->GroupName(), expected_output_.group_name);
  ASSERT_EQ(parser_->PerInstanceNames(), expected_output_.per_instance_names);
}

INSTANTIATE_TEST_SUITE_P(
    CvdParser, ValidNamesTest,
    testing::Values(
        InputOutput{.input = "--name=cf",
                    .expected = ExpectedOutput{.group_name = "cf"}},
        InputOutput{.input = "--name=cvd,cf",
                    .expected = ExpectedOutput{.per_instance_names =
                                                   std::vector<std::string>{
                                                       "cvd", "cf"}}},
        InputOutput{.input = "--name=cf-09,cf-tv",
                    .expected = ExpectedOutput{.group_name = "cf",
                                               .per_instance_names =
                                                   std::vector<std::string>{
                                                       "09", "tv"}}},
        InputOutput{
            .input = "--device_name cf-09",
            .expected = ExpectedOutput{.group_name = "cf",
                                       .per_instance_names =
                                           std::vector<std::string>{"09"}}},
        InputOutput{.input = "--device_name my_cool-phone,my_cool-tv",
                    .expected = ExpectedOutput{.group_name = "my_cool",
                                               .per_instance_names =
                                                   std::vector<std::string>{
                                                       "phone", "tv"}}},
        InputOutput{
            .input = "--group_name=my_cool --instance_name=phone",
            .expected = ExpectedOutput{.group_name = "my_cool",
                                       .per_instance_names =
                                           std::vector<std::string>{"phone"}}},
        InputOutput{.input = "--group_name=my_cool --instance_name=phone,tv",
                    .expected = ExpectedOutput{.group_name = "my_cool",
                                               .per_instance_names =
                                                   std::vector<std::string>{
                                                       "phone", "tv"}}},
        InputOutput{
            .input = "--group_name=my_cool",
            .expected =
                ExpectedOutput{
                    .group_name = "my_cool",
                }},
        InputOutput{
            .input = "--instance_name=my_cool",
            .expected = ExpectedOutput{
                .per_instance_names = std::vector<std::string>{"my_cool"}}}));

TEST_P(InvalidNamesTest, InvalidInputs) { ASSERT_FALSE(parser_); }

INSTANTIATE_TEST_SUITE_P(
    CvdParser, InvalidNamesTest,
    testing::Values("--name", "--name=?34", "--device_name=abcd",
                    "--group_name=3ab", "--name=x --device_name=y",
                    "--name=x --group_name=cf",
                    "--device_name=z --instance_name=p", "--instance_name=*79a",
                    "--device_name=abcd-e,xyz-f", "--device_name=xyz-e,xyz-e"));

}  // namespace selector
}  // namespace cuttlefish
