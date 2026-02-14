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

#include "cuttlefish/host/commands/assemble_cvd/flags/memory_mb.h"

#include <string>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/result/result.h"

DEFINE_string(memory_mb, std::to_string(CF_DEFAULTS_MEMORY_MB),
              "Total amount of memory available for guest, MB.");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "memory_mb";

}  // namespace

Result<MemoryMbFlag> MemoryMbFlag::FromGlobalGflags() {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  FromGflags<int> result = CF_EXPECT(IntFromGlobalGflags(flag_info, kFlagName));
  return MemoryMbFlag(std::move(result.values), result.is_default);
}

MemoryMbFlag::MemoryMbFlag(std::vector<int> flag_values, bool is_default)
    : FlagBase<int>(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
