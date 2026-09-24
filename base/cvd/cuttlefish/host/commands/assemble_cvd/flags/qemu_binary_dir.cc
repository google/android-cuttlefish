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

#include "cuttlefish/host/commands/assemble_cvd/flags/qemu_binary_dir.h"

#include <string>
#include <utility>
#include <vector>

#include "gflags/gflags.h"

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/result/result.h"

DEFINE_string(qemu_binary_dir, CF_DEFAULTS_QEMU_BINARY_DIR,
              "Path to the directory containing the qemu binary to use");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "qemu_binary_dir";

}  // namespace

Result<QemuBinaryDirFlag> QemuBinaryDirFlag::FromGlobalGflags() {
  const auto flag_info = gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  FromGflags<std::string> result =
      CF_EXPECT(StringFromGlobalGflags(flag_info, kFlagName));
  return QemuBinaryDirFlag(std::move(result.values), result.is_default,
                           std::move(result.is_default_values));
}

QemuBinaryDirFlag::QemuBinaryDirFlag(std::vector<std::string> flag_values,
                                     bool is_default,
                                     std::vector<bool> is_default_values)
    : FlagBase<std::string>(std::move(flag_values), is_default,
                            std::move(is_default_values)) {}

}  // namespace cuttlefish
