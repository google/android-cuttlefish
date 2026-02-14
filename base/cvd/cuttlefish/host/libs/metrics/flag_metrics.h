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

#pragma once

#include <string>
#include <vector>

#include "cuttlefish/host/libs/config/data_image_policy.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct FlagMetrics {
  int cpus;
  bool daemon;
  DataImagePolicy data_policy;
  std::string extra_kernel_cmdline;
  GpuMode gpu_mode;
  bool guest_enforce_security;
  int memory_mb;
  bool restart_subprocesses;
  bool system_image_dir_specified;
};

// depends on gflags::ParseCommandLineFlags being called previously
Result<std::vector<FlagMetrics>> GetFlagMetrics(int guest_count);

}  // namespace cuttlefish
