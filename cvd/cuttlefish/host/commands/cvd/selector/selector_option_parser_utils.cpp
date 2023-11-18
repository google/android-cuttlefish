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

#include "host/commands/cvd/selector/selector_option_parser_utils.h"

#include <android-base/strings.h>

namespace cuttlefish {
namespace selector {

Result<std::vector<std::string>> SeparateButWithNoEmptyToken(
    const std::string& input, const std::string& delimiter) {
  auto tokens = android::base::Split(input, delimiter);
  for (const auto& t : tokens) {
    CF_EXPECT(!t.empty());
  }
  return tokens;
}

}  // namespace selector
}  // namespace cuttlefish
