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

#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "host/commands/cvd/selector/start_selector_parser.h"
#include "host/commands/cvd/unittests/selector/parser_ids_helper.h"

namespace cuttlefish {
namespace selector {

TEST_P(InstanceIdTest, InstanceIdCalculation) {
  auto parser = StartSelectorParser::ConductSelectFlagsParser(
      uid_, selector_args_, cmd_args_, envs_);

  ASSERT_EQ(parser.ok(), expected_result_);
  if (!expected_result_) {
    return;
  }
  ASSERT_EQ(parser->InstanceIds(), expected_ids_);
  ASSERT_EQ(parser->RequestedNumInstances(), requested_num_instances_);
}

INSTANTIATE_TEST_SUITE_P(
    CvdParser, InstanceIdTest,
    testing::Values(
        InstanceIdTestInput{.cuttlefish_instance = std::nullopt,
                            .expected_ids = std::nullopt,
                            .requested_num_instances = 1,
                            .expected_result = true},
        InstanceIdTestInput{.cuttlefish_instance = "8",
                            .expected_ids = std::vector<unsigned>{8},
                            .requested_num_instances = 1,
                            .expected_result = true},
        InstanceIdTestInput{.cmd_args = "--num_instances=2",
                            .expected_ids = std::nullopt,
                            .requested_num_instances = 2,
                            .expected_result = true},
        InstanceIdTestInput{.cmd_args = "--num_instances=2",
                            .cuttlefish_instance = "8",
                            .expected_ids = std::vector<unsigned>{8, 9},
                            .requested_num_instances = 2,
                            .expected_result = true},
        InstanceIdTestInput{
            .cmd_args = "--base_instance_num=10 --num_instances=2",
            .cuttlefish_instance = "8",
            .expected_ids = std::vector<unsigned>{10, 11},
            .requested_num_instances = 2,
            .expected_result = true},
        InstanceIdTestInput{.cmd_args = "--instance_nums 2",
                            .cuttlefish_instance = std::nullopt,
                            .expected_ids = std::vector<unsigned>{2},
                            .requested_num_instances = 1,
                            .expected_result = true},
        InstanceIdTestInput{.cmd_args = "--instance_nums 2,5,6",
                            .cuttlefish_instance = std::nullopt,
                            .expected_ids = std::vector<unsigned>{2, 5, 6},
                            .requested_num_instances = 3,
                            .expected_result = true},
        InstanceIdTestInput{
            .cmd_args = "--instance_nums 2,5,6 --num_instances=3",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::vector<unsigned>{2, 5, 6},
            .requested_num_instances = 3,
            .expected_result = true},
        InstanceIdTestInput{
            .cmd_args = "--instance_nums 2,5,6 --num_instances=3",
            .selector_args = "--instance_name=c-1,c-3,c-5",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::vector<unsigned>{2, 5, 6},
            .requested_num_instances = 3,
            .expected_result = true},
        InstanceIdTestInput{.selector_args = "--instance_name=c-1,c-3,c-5",
                            .cuttlefish_instance = std::nullopt,
                            .expected_ids = std::nullopt,
                            .requested_num_instances = 3,
                            .expected_result = true},
        // CUTTLEFISH_INSTANCE should be ignored
        InstanceIdTestInput{
            .cmd_args = "--instance_nums 2,5,6 --num_instances=3",
            .cuttlefish_instance = "8",
            .expected_ids = std::vector<unsigned>{2, 5, 6},
            .requested_num_instances = 3,
            .expected_result = true},
        // instance_nums and num_instances mismatch
        InstanceIdTestInput{
            .cmd_args = "--instance_nums 2,5,6 --num_instances=7",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::vector<unsigned>{2, 5, 6},
            .requested_num_instances = 3,
            .expected_result = false},
        // --instance_name requested 2 instances while instance_nums 3.
        InstanceIdTestInput{
            .cmd_args = "--num_instances=3 --instance_nums 2,5,6",
            .selector_args = "--instance_name=c-1,c-3",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::vector<unsigned>{2, 5, 6},
            .requested_num_instances = 3,
            .expected_result = false},
        // --base_instance_num is not allowed with --instance_nums
        InstanceIdTestInput{
            .cmd_args = "--instance_nums 2,5,6 --base_instance_num=7",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::vector<unsigned>{2, 5, 6},
            .requested_num_instances = 3,
            .expected_result = false}));

}  // namespace selector
}  // namespace cuttlefish
