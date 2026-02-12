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

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/result/result.h"

DEFINE_string(use_cvdalloc, "unset", "Acquire static resources with cvdalloc.");

namespace cuttlefish {

Result<UseCvdallocFlag> UseCvdallocFlag::FromGlobalGflags(
    const Defaults &defaults) {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie("use_cvdalloc");
  bool flag_default = defaults.BoolValue("use_cvdalloc").value_or(false);
  std::vector<bool> flag_values =
      CF_EXPECT(BoolFromGlobalGflags(flag_info, "use_cvdalloc", flag_default));
  return UseCvdallocFlag(std::move(flag_values));
}

UseCvdallocFlag::UseCvdallocFlag(std::vector<bool> flag_values)
    : FlagBase<bool>(std::move(flag_values)) {}

}  // namespace cuttlefish
