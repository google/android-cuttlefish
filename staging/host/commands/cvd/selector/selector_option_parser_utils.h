/*
 * Copyright (C) 2022 The Android Open Source Project
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

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_database_utils.h"

namespace cuttlefish {
namespace selector {

/*
 * @return any parsing successfully and actually happened
 */
template <typename T>
Result<void> FilterSelectorFlag(std::vector<std::string>& args,
                                const std::string& flag_name,
                                std::optional<T>& value_opt) {
  value_opt = std::nullopt;
  const int args_initial_size = args.size();
  if (args_initial_size == 0) {
    return {};
  }

  T value;
  CF_EXPECT(ParseFlags({GflagsCompatFlag(flag_name, value)}, args),
            "Failed to parse --" << flag_name);
  if (args.size() == args_initial_size) {
    // not consumed
    return {};
  }
  value_opt = value;
  return {};
}

struct VerifyNameOptionsParam {
  std::optional<std::string> device_name;
  std::optional<std::string> group_name;
  std::optional<std::string> per_instance_name;
};

/*
 * There are valid combinations of --device_name, --group_name, and
 * --instance_name, let alone the syntax of each.
 *
 * --device_name respectively should be given without any of the
 * other two. --group_name and --instance_name could be given together.
 *
 * It is allowed that none of those three options is given.
 */
Result<void> VerifyNameOptions(const VerifyNameOptionsParam& param);

Result<DeviceName> SplitDeviceName(const std::string& device_name);

/*
 * android::base::Split by delimeter but returns CF_ERR if any split token is
 * empty
 */
Result<std::vector<std::string>> SeparateButWithNoEmptyToken(
    const std::string& input, const std::string& delimiter);

}  // namespace selector
}  // namespace cuttlefish
