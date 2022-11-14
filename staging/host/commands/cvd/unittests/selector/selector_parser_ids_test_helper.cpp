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

#include "host/commands/cvd/unittests/selector/selector_parser_ids_test_helper.h"

#include <android-base/strings.h>

#include "host/commands/cvd/selector/selector_cmdline_parser.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace selector {

InstanceIdTest::InstanceIdTest() {
  auto [input, cuttlefish_instance, ids, num_instances, result] = GetParam();
  auto args = android::base::Tokenize(input, " ");
  flag_separation_result_ = GetCommandAndSelectorArguments(args);
  if (!flag_separation_result_.ok()) {
    return;
  }
  auto [cmd_args, selector_args] = *flag_separation_result_;
  if (cuttlefish_instance) {
    envs_[kCuttlefishInstanceEnvVarName] = cuttlefish_instance.value();
  }
  auto parse_result = SelectorFlagsParser::ConductSelectFlagsParser(
      selector_args, cmd_args, envs_);
  if (parse_result.ok()) {
    parser_ = std::move(*parse_result);
  }
  expected_ids_ = std::move(ids);
  expected_result_ = result;
  requested_num_instances_ = num_instances;
}

}  // namespace selector
}  // namespace cuttlefish
