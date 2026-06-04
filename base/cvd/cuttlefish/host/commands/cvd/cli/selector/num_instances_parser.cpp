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

#include <stdint.h>

#include <functional>
#include <set>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace selector {

size_t NumInstancesParser::NumInstances() const {
  if (num_instances_) {
    return *num_instances_;
  }
  if (instance_nums_) {
    return instance_nums_->size();
  }
  if (num_instance_names_) {
    return *num_instance_names_;
  }

  return 1;
}

std::vector<uint32_t> NumInstancesParser::InstanceIds() const {
  if (instance_nums_) {
    return *instance_nums_;
  }
  if (!base_instance_num_) {
    return {};
  }
  std::vector<uint32_t> ids;
  for (size_t i = 0; i < NumInstances(); ++i) {
    ids.push_back(*base_instance_num_ + i);
  }
  return ids;
}

std::vector<Flag> NumInstancesParser::Flags(
    const selector::SelectorOptions& selector_options) {
  if (selector_options.instance_names) {
    num_instance_names_ = selector_options.instance_names->size();
  }
  return {
      GflagsCompatFlag("num_instances", num_instances_)
          .AddValidator(std::bind_front(&NumInstancesParser::Validate, this))
          .Help("Number of instances to create in the group. Must be a whole "
                "number larger than 0. Defaults to 1 if not provided."),
      GflagsCompatFlag("instance_nums", instance_nums_)
          .AddValidator(std::bind_front(&NumInstancesParser::Validate, this))
          .Help("[Deprecated] The instances's internal ids, as a comma "
                "separated list. This flag is only provided for backwards "
                "compatibility. It's recommended to let cvd choose the "
                "instance ids on its own."),
      GflagsCompatFlag("base_instance_num", base_instance_num_)
          .AddValidator(std::bind_front(&NumInstancesParser::Validate, this))
          .Help("[Deprecated] `--base_instance_num={base} "
                "--num_instances={count}` is equivalent to "
                "`--instance_nums={base},{base + 1},...,{base + count -1}`.")};
}

Result<void> NumInstancesParser::Validate() const {
  // The number of instances must be consistent when provided
  if (num_instances_ && num_instance_names_) {
    CF_EXPECT_EQ(*num_instances_, *num_instance_names_,
                 "The number of instances requested by --num_instances is not "
                 "the same as what is implied by --instance_name.");
  }
  if (num_instances_ && instance_nums_) {
    CF_EXPECT_EQ(*num_instances_, instance_nums_->size(),
                 "Number of instance ids provided with --instance_nums doesn't "
                 "match value provided with --num_instances.");
  }
  if (instance_nums_ && num_instance_names_) {
    CF_EXPECT_EQ(instance_nums_->size(), *num_instance_names_,
                 "--instance_nums and --instance_name must have the same "
                 "cardinality when provided");
  }

  // The provided instance numbers must be distinct
  if (instance_nums_) {
    std::set<int32_t> instance_nums;
    for (int32_t num : *instance_nums_) {
      CF_EXPECTF(instance_nums.find(num) == instance_nums.end(),
                 "Instance ids must be unique, but {} is repeated.", num);
    }
  }

  CF_EXPECT(!instance_nums_ || !base_instance_num_,
            "--base_instance_num and --instance_nums are mutually exclusive");
  return {};
}

}  // namespace selector
}  // namespace cuttlefish
