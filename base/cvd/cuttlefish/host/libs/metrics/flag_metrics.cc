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

#include "cuttlefish/host/libs/metrics/flag_metrics.h"

#include <vector>

#include "cuttlefish/host/libs/metrics/parsed_flags.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::vector<FlagMetrics>> GetFlagMetrics(const ParsedFlags& parsed_flags,
                                                const int guest_count) {
  std::vector<FlagMetrics> result;
  result.reserve(guest_count);

  for (int i = 0; i < guest_count; i++) {
    result.emplace_back(FlagMetrics{
        .cpus = parsed_flags.cpus.ForIndex(i),
        .daemon = parsed_flags.daemon.ForIndex(i),
        .data_policy = parsed_flags.data_policy.ForIndex(i),
        // the same extra_kernel_cmdline value is used for all guests
        .extra_kernel_cmdline = parsed_flags.extra_kernel_cmdline.ForIndex(0),
        .gpu_mode = parsed_flags.gpu_mode.ForIndex(i),
        .guest_enforce_security =
            parsed_flags.guest_enforce_security.ForIndex(i),
        .memory_mb = parsed_flags.memory_mb.ForIndex(i),
        .restart_subprocesses = parsed_flags.restart_subprocesses.ForIndex(i),
        .system_image_dir_specified =
            !parsed_flags.system_image_dir.IsDefault(),
    });
  }
  return result;
}

}  // namespace cuttlefish
