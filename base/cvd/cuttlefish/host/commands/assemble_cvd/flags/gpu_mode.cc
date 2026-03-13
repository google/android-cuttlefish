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

#include "cuttlefish/host/commands/assemble_cvd/flags/gpu_mode.h"

#include <string>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/result/result.h"

DEFINE_string(
    gpu_mode, CF_DEFAULTS_GPU_MODE,
    "What gpu configuration to use.  One of {auto, custom, drm_virgl, "
    "gfxstream, gfxstream_guest_angle, gfxstream_guest_angle_host_lavapipe, "
    "gfxstream_guest_angle_host_swiftshader, guest_swiftshader, none}");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "gpu_mode";

}  // namespace

Result<GpuModeFlag> GpuModeFlag::FromGlobalGflags() {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  FromGflags<std::string> result =
      CF_EXPECT(StringFromGlobalGflags(flag_info, kFlagName));
  std::vector<GpuMode> flag_values;
  for (const auto& value : result.values) {
    flag_values.emplace_back(CF_EXPECT(GpuModeFromString(value)));
  }
  return GpuModeFlag(std::move(flag_values), result.is_default);
}

GpuModeFlag::GpuModeFlag(std::vector<GpuMode> flag_values, bool is_default)
    : FlagBase<GpuMode>(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
