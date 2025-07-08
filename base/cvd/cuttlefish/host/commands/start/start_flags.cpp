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

#include "cuttlefish/host/commands/start/start_flags.h"

#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_int32(num_instances, CF_DEFAULTS_NUM_INSTANCES,
             "Number of Android guests to launch");
DEFINE_string(report_anonymous_usage_stats,
              CF_DEFAULTS_REPORT_ANONYMOUS_USAGE_STATS,
              "Report anonymous usage "
              "statistics for metrics collection and analysis.");
DEFINE_int32(
    base_instance_num, CF_DEFAULTS_BASE_INSTANCE_NUM,
    "The instance number of the device created. When `-num_instances N`"
    " is used, N instance numbers are claimed starting at this number.");
DEFINE_string(instance_nums, CF_DEFAULTS_INSTANCE_NUMS,
              "A comma-separated list of instance numbers "
              "to use. Mutually exclusive with base_instance_num.");
DEFINE_string(verbosity, CF_DEFAULTS_VERBOSITY,
              "Console logging verbosity. Options are VERBOSE,"
              "DEBUG,INFO,WARNING,ERROR");
DEFINE_string(file_verbosity, CF_DEFAULTS_FILE_VERBOSITY,
              "Log file logging verbosity. Options are VERBOSE,DEBUG,INFO,"
              "WARNING,ERROR");
DEFINE_bool(use_overlay, CF_DEFAULTS_USE_OVERLAY,
            "Capture disk writes an overlay. This is a "
            "prerequisite for powerwash_cvd or multiple instances.");
DEFINE_bool(track_host_tools_crc, CF_DEFAULTS_TRACK_HOST_TOOLS_CRC,
            "Track changes to host executables");
