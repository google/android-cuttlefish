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

#include "cuttlefish/host/commands/assemble_cvd/flags/daemon.h"

#include <string>
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/bool_flag.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(daemon, CF_DEFAULTS_DAEMON ? "true" : "false",
              "Run cuttlefish in background, the launcher exits on boot "
              "completed/failed");

namespace cuttlefish {

Result<DaemonFlag> DaemonFlag::FromGlobalGflags() {
  auto bool_flag = CF_EXPECT(BoolFlag::FromGlobalGflagsAndName("daemon"));
  return DaemonFlag(std::move(bool_flag));
}

DaemonFlag::DaemonFlag(BoolFlag&& bool_flag) : BoolFlag(std::move(bool_flag)) {}

}  // namespace cuttlefish
