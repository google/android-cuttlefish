/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/flags/parser.h"

#include <string>
#include <string_view>

#include "absl/strings/numbers.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<bool> ParseBool(std::string_view value, std::string_view name) {
  bool result;
  CF_EXPECTF(absl::SimpleAtob(value, &result),
             "Failed to parse value \"{}\" for {}", value, name);
  return result;
}

Result<int> ParseInt(const std::string& value, std::string_view name) {
  int result;
  CF_EXPECTF(absl::SimpleAtoi(value, &result),
             "Failed to parse value \"{}\" as integer for \"{}\"", value, name);
  return result;
}

}  // namespace cuttlefish
