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

#include "cuttlefish/host/commands/assemble_cvd/flags/vhost_user_block.h"

#include <utility>
#include <vector>

#include "gflags/gflags.h"

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/result/result.h"

DEFINE_string(
    vhost_user_block, "unset",
    "(experimental) use crosvm vhost-user block device implementation ");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "vhost_user_block";

}  // namespace

Result<VhostUserBlockFlag> VhostUserBlockFlag::FromGlobalGflags(
    const VmManagerFlag& vm_manager) {
  const gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  FromGflags<bool> result = CF_EXPECT(
      BoolFromGlobalGflags(flag_info, kFlagName, VmManagerIsQemu(vm_manager)));
  for (const bool value : result.values) {
    if (value) {
      CF_EXPECT(VmManagerIsCrosvm(vm_manager) || VmManagerIsQemu(vm_manager),
                "vhost-user block only supported on crosvm and qemu");
    }
  }
  return VhostUserBlockFlag(std::move(result.values), result.is_default);
}

VhostUserBlockFlag::VhostUserBlockFlag(std::vector<bool> flag_values,
                                       bool is_default)
    : FlagBase<bool>(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
