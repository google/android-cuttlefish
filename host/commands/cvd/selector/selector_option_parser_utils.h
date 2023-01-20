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

/*
 * android::base::Split by delimeter but returns CF_ERR if any split token is
 * empty
 */
Result<std::vector<std::string>> SeparateButWithNoEmptyToken(
    const std::string& input, const std::string& delimiter);

}  // namespace selector
}  // namespace cuttlefish
