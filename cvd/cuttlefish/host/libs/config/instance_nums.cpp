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

#include "host/libs/config/instance_nums.h"

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/utils/flag_parser.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

// Failed result: The flag was specified in an invalid way
// Empty optional: The flag was not specified
// Present optional: The flag was specified with a valid value
static Result<std::optional<std::int32_t>> ParseBaseInstanceFlag(
    std::vector<std::string>& flags) {
  int value = -1;
  auto flag = GflagsCompatFlag("base_instance_num", value);
  CF_EXPECT(flag.Parse(flags), "Flag parsing error");
  return value > 0 ? value : std::optional<std::int32_t>();
}

// Failed result: The flag was specified in an invalid way
// Empty optional: The flag was not specified
// Present optional: The flag was specified with a valid value
static Result<std::optional<std::int32_t>> ParseNumInstancesFlag(
    std::vector<std::string>& flags) {
  int value = -1;
  auto flag = GflagsCompatFlag("num_instances", value);
  CF_EXPECT(flag.Parse(flags), "Flag parsing error");
  return value > 0 ? value : std::optional<std::int32_t>();
}

// Failed result: The flag was specified in an invalid way
// Empty set: The flag was not specified
// Set with members: The flag was specified with a valid value
static Result<std::set<std::int32_t>> ParseInstanceNums(
    const std::string& instance_nums_str) {
  if (instance_nums_str == "") {
    return {};
  }
  std::set<std::int32_t> instance_nums;
  std::vector<std::string> split_str =
      android::base::Split(instance_nums_str, ",");
  for (const auto& instance_num_str : split_str) {
    std::int32_t instance_num;
    CF_EXPECT(android::base::ParseInt(instance_num_str.c_str(), &instance_num),
              "Unable to parse \"" << instance_num_str << "\" in "
                                   << "`--instance_nums=\"" << instance_nums_str
                                   << "\"`");
    instance_nums.insert(instance_num);
  }
  return instance_nums;
}

// Failed result: The flag was specified in an invalid way
// Empty set: The flag was not specified
// Set with members: The flag was specified with a valid value
static Result<std::set<std::int32_t>> ParseInstanceNumsFlag(
    std::vector<std::string>& flags) {
  std::string value;
  auto flag = GflagsCompatFlag("instance_nums", value);
  CF_EXPECT(flag.Parse(flags), "Flag parsing error");
  if (value == "") {
    return CF_EXPECT(ParseInstanceNums(value));
  } else {
    return {};
  }
}

InstanceNumsCalculator& InstanceNumsCalculator::FromFlags(
    const std::vector<std::string>& flags) & {
  std::vector<std::string> flags_copy = flags;
  TrySet(base_instance_num_, ParseBaseInstanceFlag(flags_copy));
  TrySet(num_instances_, ParseNumInstancesFlag(flags_copy));
  TrySet(instance_nums_, ParseInstanceNumsFlag(flags_copy));
  return *this;
}

InstanceNumsCalculator InstanceNumsCalculator::FromFlags(
    const std::vector<std::string>& flags) && {
  return FromFlags(flags);
}

// Failed result: The flag was specified in an invalid way
// Empty optional: The flag was not specified
// Present optional: The flag was specified with a valid value
static Result<std::optional<std::int32_t>> GflagsBaseInstanceFlag() {
  gflags::CommandLineFlagInfo info;
  if (!gflags::GetCommandLineFlagInfo("base_instance_num", &info)) {
    return {};
  }
  if (info.is_default) {
    return {};
  }
  CF_EXPECT(info.type == "int32");
  return *reinterpret_cast<const std::int32_t*>(info.flag_ptr);
}

// Failed result: The flag was specified in an invalid way
// Empty optional: The flag was not specified
// Present optional: The flag was specified with a valid value
static Result<std::optional<std::int32_t>> GflagsNumInstancesFlag() {
  gflags::CommandLineFlagInfo info;
  if (!gflags::GetCommandLineFlagInfo("num_instances", &info)) {
    return {};
  }
  if (info.is_default) {
    return {};
  }
  CF_EXPECT(info.type == "int32");
  return *reinterpret_cast<const std::int32_t*>(info.flag_ptr);
}

// Failed result: The flag was specified in an invalid way
// Empty set: The flag was not specified
// Set with members: The flag was specified with a valid value
static Result<std::set<std::int32_t>> GflagsInstanceNumsFlag() {
  gflags::CommandLineFlagInfo info;
  if (!gflags::GetCommandLineFlagInfo("instance_nums", &info)) {
    return {};
  }
  if (info.is_default) {
    return {};
  }
  CF_EXPECT(info.type == "string");
  auto contents = *reinterpret_cast<const std::string*>(info.flag_ptr);
  return CF_EXPECT(ParseInstanceNums(contents));
}

InstanceNumsCalculator& InstanceNumsCalculator::FromGlobalGflags() & {
  TrySet(base_instance_num_, GflagsBaseInstanceFlag());
  TrySet(num_instances_, GflagsNumInstancesFlag());
  TrySet(instance_nums_, GflagsInstanceNumsFlag());
  return *this;
}

InstanceNumsCalculator InstanceNumsCalculator::FromGlobalGflags() && {
  return FromGlobalGflags();
}

InstanceNumsCalculator& InstanceNumsCalculator::BaseInstanceNum(
    std::int32_t num) & {
  base_instance_num_ = num;
  return *this;
}
InstanceNumsCalculator InstanceNumsCalculator::BaseInstanceNum(
    std::int32_t num) && {
  return BaseInstanceNum(num);
}

InstanceNumsCalculator& InstanceNumsCalculator::NumInstances(
    std::int32_t num) & {
  num_instances_ = num;
  return *this;
}
InstanceNumsCalculator InstanceNumsCalculator::NumInstances(
    std::int32_t num) && {
  return NumInstances(num);
}

InstanceNumsCalculator& InstanceNumsCalculator::InstanceNums(
    const std::string& nums) & {
  TrySet(instance_nums_, ParseInstanceNums(nums));
  return *this;
}
InstanceNumsCalculator InstanceNumsCalculator::InstanceNums(
    const std::string& nums) && {
  return InstanceNums(nums);
}

InstanceNumsCalculator& InstanceNumsCalculator::InstanceNums(
    std::set<std::int32_t> set) & {
  instance_nums_ = std::move(set);
  return *this;
}
InstanceNumsCalculator InstanceNumsCalculator::InstanceNums(
    std::set<std::int32_t> set) && {
  return InstanceNums(std::move(set));
}

template <typename T>
void InstanceNumsCalculator::TrySet(T& field, Result<T> result) {
  if (result.ok()) {
    field = std::move(*result);
  } else {
    // TODO(schuffelen): Combine both errors into one
    setter_result_.error() = result.error();
  }
}

Result<std::set<std::int32_t>> InstanceNumsCalculator::Calculate() {
  CF_EXPECT(Result<void>(setter_result_));
  if (!instance_nums_.empty() && base_instance_num_) {
    return CF_ERR("InstanceNums and BaseInstanceNum are mutually exclusive");
  }
  if (!instance_nums_.empty()) {
    if (num_instances_) {
      CF_EXPECT(instance_nums_.size() == *num_instances_);
    }
    CF_EXPECT(instance_nums_.size() > 0, "no instance nums");
    return instance_nums_;
  }
  std::set<std::int32_t> instance_nums;
  for (int i = 0; i < num_instances_.value_or(1); i++) {
    instance_nums.insert(i + base_instance_num_.value_or(GetInstance()));
  }
  CF_EXPECT(instance_nums.size() > 0, "no instance nums");
  return instance_nums;
}

}  // namespace cuttlefish
