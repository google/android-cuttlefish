/*
 * Copyright (C) 2023 The Android Open Source Project
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

#pragma once

#include "common/libs/utils/result.h"
#include "host/commands/cvd/client.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

Result<void> HandleReset(CvdClient& client,
                         const cvd_common::Args& subcmd_args);

static constexpr char kHelpMessage[] = R"(usage: cvd reset <args>

* Warning: Cvd reset is an experimental implementation. When you are in panic,
cvd reset is the last resort.

args:
  --help                 Prints this message.
    help

  --device-by-cvd-only   Terminates devices that a cvd server started
                         This excludes the devices launched by "launch_cvd"
                         or "cvd_internal_start" directly (default: false)

  --clean-runtime-dir    Cleans up the runtime directory for the devices
                         Yet to be implemented. For now, if true, only if
                         stop_cvd supports --clear_instance_dirs and the
                         device could be stopped by stop_cvd, the flag takes
                         effects. (default: true)

  --yes                  Resets without asking the user confirmation.
   -y

description:

  1. Gracefully stops all devices that the cvd client can reach.
  2. Forcefully stops all run_cvd processes and their subprocesses.
  3. Kill the cvd server itself if unresponsive.
  4. Reset the states of the involved instance lock files
     -- If cvd reset stops a device, it resets the corresponding lock file.
  5. Optionally, cleans up the runtime files of the stopped devices.)";

}  // namespace cuttlefish
