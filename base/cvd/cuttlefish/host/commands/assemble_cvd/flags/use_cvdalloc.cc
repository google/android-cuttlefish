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
#include "cuttlefish/host/commands/assemble_cvd/flags/use_cvdalloc.h"

#include <stddef.h>

#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"

DEFINE_string(use_cvdalloc, "unset", "Acquire static resources with cvdalloc.");

namespace cuttlefish {

Result<UseCvdallocFlag> UseCvdallocFlag::FromGlobalGflags() {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie("use_cvdalloc");
  std::vector<bool> flag_values =
      CF_EXPECT(BoolFromGlobalGflags(flag_info, "use_cvdalloc", false));
  return UseCvdallocFlag(std::move(flag_values));
}

bool UseCvdallocFlag::ForIndex(const std::size_t index) const {
  if (index < values_.size()) {
    return values_[index];
  } else {
    return values_[0];
  }
}

UseCvdallocFlag::UseCvdallocFlag(std::vector<bool> values)
    : values_(std::move(values)) {}

}  // namespace cuttlefish
