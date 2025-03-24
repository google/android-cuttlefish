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

#pragma once

#include <gflags/gflags.h>

DECLARE_int32(num_instances);
DECLARE_string(report_anonymous_usage_stats);
DECLARE_int32(base_instance_num);
DECLARE_string(instance_nums);
DECLARE_string(verbosity);
DECLARE_string(file_verbosity);
DECLARE_bool(use_overlay);
DECLARE_bool(track_host_tools_crc);
