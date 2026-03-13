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

#include "cuttlefish/host/commands/assemble_cvd/flags/extra_kernel_cmdline.h"

#include <string>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(extra_kernel_cmdline, CF_DEFAULTS_EXTRA_KERNEL_CMDLINE,
              "Additional flags to put on the kernel command line");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "extra_kernel_cmdline";

}  // namespace

ExtraKernelCmdlineFlag ExtraKernelCmdlineFlag::FromGlobalGflags() {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  return ExtraKernelCmdlineFlag({flag_info.current_value},
                                flag_info.is_default);
}

ExtraKernelCmdlineFlag::ExtraKernelCmdlineFlag(
    std::vector<std::string> flag_values, bool is_default)
    : FlagBase<std::string>(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
