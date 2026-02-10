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

#include "cuttlefish/host/commands/assemble_cvd/flags/guest_enforce_security.h"

#include <utility>
#include <vector>

#include <fmt/format.h>
#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/result/result.h"

DEFINE_string(guest_enforce_security,
              fmt::format("{}", CF_DEFAULTS_GUEST_ENFORCE_SECURITY),
              "Whether to run in enforcing mode (non permissive).");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "guest_enforce_security";

}  // namespace

Result<GuestEnforceSecurityFlag> GuestEnforceSecurityFlag::FromGlobalGflags() {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  FromGflags<bool> result =
      CF_EXPECT(BoolFromGlobalGflags(flag_info, kFlagName));
  return GuestEnforceSecurityFlag(std::move(result.values), result.is_default);
}

GuestEnforceSecurityFlag::GuestEnforceSecurityFlag(
    std::vector<bool> flag_values, bool is_default)
    : FlagBase<bool>(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
