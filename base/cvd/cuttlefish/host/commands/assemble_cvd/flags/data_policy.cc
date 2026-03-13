/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/flags/data_policy.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/data_image_policy.h"
#include "cuttlefish/result/result.h"

DEFINE_string(data_policy, CF_DEFAULTS_DATA_POLICY,
              "How to handle userdata partition. Either 'use_existing', "
              "'resize_up_to', or 'always_create'.");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "data_policy";

}  // namespace

Result<DataPolicyFlag> DataPolicyFlag::FromGlobalGflags() {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  FromGflags<std::string> result =
      CF_EXPECT(StringFromGlobalGflags(flag_info, kFlagName));
  std::vector<DataImagePolicy> flag_values;
  std::transform(result.values.cbegin(), result.values.cend(),
                 std::back_inserter(flag_values), DataImagePolicyFromString);
  return DataPolicyFlag(std::move(flag_values), result.is_default);
}

DataPolicyFlag::DataPolicyFlag(std::vector<DataImagePolicy> flag_values,
                               bool is_default)
    : FlagBase<DataImagePolicy>(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
