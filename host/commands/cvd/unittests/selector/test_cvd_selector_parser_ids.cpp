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

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "host/commands/cvd/selector/selector_cmdline_parser.h"
#include "host/commands/cvd/selector/selector_option_parser_utils.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace selector {

using Envs = std::unordered_map<std::string, std::string>;
using Args = std::vector<std::string>;

struct InstanceIdTestInput {
  std::string input_args;
  std::optional<std::string> cuttlefish_instance;
  std::optional<std::unordered_set<unsigned>> expected_ids;
  bool expected_result;
};

class InstanceIdTest : public testing::TestWithParam<InstanceIdTestInput> {
 protected:
  InstanceIdTest() {
    auto [input, cuttlefish_instance, ids, result] = GetParam();
    auto cmd_args = android::base::Tokenize(input, " ");
    if (cuttlefish_instance) {
      envs_[kCuttlefishInstanceEnvVarName] = cuttlefish_instance.value();
    }
    auto parse_result =
        SelectorFlagsParser::ConductSelectFlagsParser(Args{}, cmd_args, envs_);
    if (parse_result.ok()) {
      parser_ = std::move(*parse_result);
    }
    expected_ids_ = std::move(ids);
    expected_result_ = result;
  }
  bool expected_result_;
  std::optional<std::unordered_set<unsigned>> expected_ids_;
  std::unordered_map<std::string, std::string> envs_;
  std::optional<SelectorFlagsParser> parser_;
};

TEST_P(InstanceIdTest, InstanceIdCalculation) {
  ASSERT_EQ(parser_ != std::nullopt, expected_result_);
  if (!expected_result_) {
    return;
  }
  ASSERT_EQ(parser_->InstanceIds(), expected_ids_);
}

INSTANTIATE_TEST_SUITE_P(
    CvdParser, InstanceIdTest,
    testing::Values(
        InstanceIdTestInput{.input_args = "",
                            .cuttlefish_instance = std::nullopt,
                            .expected_ids = std::nullopt,
                            .expected_result = true},
        InstanceIdTestInput{.input_args = "",
                            .cuttlefish_instance = "8",
                            .expected_ids = std::unordered_set<unsigned>{8},
                            .expected_result = true},
        InstanceIdTestInput{.input_args = "--num_instances=2",
                            .cuttlefish_instance = "8",
                            .expected_ids = std::unordered_set<unsigned>{8, 9},
                            .expected_result = true},
        InstanceIdTestInput{
            .input_args = "--base_instance_num=10 --num_instances=2",
            .cuttlefish_instance = "8",
            .expected_ids = std::unordered_set<unsigned>{10, 11},
            .expected_result = true},
        InstanceIdTestInput{.input_args = "--instance_nums 2",
                            .cuttlefish_instance = std::nullopt,
                            .expected_ids = std::unordered_set<unsigned>{2},
                            .expected_result = true},
        InstanceIdTestInput{
            .input_args = "--instance_nums 2,5,6",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::unordered_set<unsigned>{2, 5, 6},
            .expected_result = true},
        InstanceIdTestInput{
            .input_args = "--instance_nums 2,5,6 --num_instances=3",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::unordered_set<unsigned>{2, 5, 6},
            .expected_result = true},
        InstanceIdTestInput{
            .input_args = "--instance_nums 2,5,6 --num_instances=7",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::unordered_set<unsigned>{2, 5, 6},
            .expected_result = false},
        // CUTTLEFISH_INSTANCE should be ignored
        InstanceIdTestInput{
            .input_args = "--instance_nums 2,5,6 --num_instances=3",
            .cuttlefish_instance = "8",
            .expected_ids = std::unordered_set<unsigned>{2, 5, 6},
            .expected_result = true},
        // --base_instance_num is not allowed with --instance_nums
        InstanceIdTestInput{
            .input_args = "--instance_nums 2,5,6 --base_instance_num=7",
            .cuttlefish_instance = std::nullopt,
            .expected_ids = std::unordered_set<unsigned>{2, 5, 6},
            .expected_result = false}));

}  // namespace selector
}  // namespace cuttlefish
