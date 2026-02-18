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

#include "cuttlefish/host/commands/assemble_cvd/flags/blank_data_image_mb.h"

#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/result/result.h"

DEFINE_string(blank_data_image_mb, CF_DEFAULTS_BLANK_DATA_IMAGE_MB,
              "The size of the blank data image to generate, MB.");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "blank_data_image_mb";

}  // namespace

Result<BlankDataImageMbFlag> BlankDataImageMbFlag::FromGlobalGflags(
    const std::vector<GuestConfig>& guest_configs) {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  FromGflags<int> result = CF_EXPECT(IntFromGlobalGflags(flag_info, kFlagName));

  if (guest_configs.size() > result.values.size()) {
    result.values.resize(guest_configs.size(), -1);
    for (int i = result.values.size(); i < guest_configs.size(); i++) {
      result.values[i] = guest_configs[i].blank_data_image_mb;
    }
  }

  return BlankDataImageMbFlag(std::move(result.values), result.is_default);
}

BlankDataImageMbFlag::BlankDataImageMbFlag(std::vector<int> flag_values,
                                           bool is_default)
    : FlagBase<int>(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
