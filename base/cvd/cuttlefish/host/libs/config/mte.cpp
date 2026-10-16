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

#include "cuttlefish/host/libs/config/mte.h"

#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<Mte> ParseMte(std::string_view str) {
  if (absl::EqualsIgnoreCase(str, "auto")) {
    return Mte::kAuto;
  }
  bool bool_res;
  CF_EXPECTF(absl::SimpleAtob(str, &bool_res),
             "Failed to parse mte option \"{}\"", str);
  return bool_res ? Mte::kOn : Mte::kOff;
}

}  // namespace cuttlefish
