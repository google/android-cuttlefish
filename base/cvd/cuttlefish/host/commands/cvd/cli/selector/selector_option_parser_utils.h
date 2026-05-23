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

#include <stddef.h>

#include <optional>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace selector {

/*
 * @return any parsing successfully and actually happened
 */
template <typename T>
Result<std::optional<T>> FilterSelectorFlag(std::vector<std::string>& args,
                                            const std::string& flag_name) {
  std::optional<T> value_opt;
  CF_EXPECT(ConsumeFlags(
                {GflagsCompatFlag(flag_name, value_opt, CoerceToNullopt::None)},
                args),
            "Failed to parse --" << flag_name);
  return value_opt;
}

}  // namespace selector
}  // namespace cuttlefish
