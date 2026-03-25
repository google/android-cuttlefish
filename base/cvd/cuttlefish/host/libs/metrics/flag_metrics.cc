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

#include "cuttlefish/host/libs/metrics/parsed_flags.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<FlagMetrics> GetFlagMetrics(const ParsedFlags& parsed_flags,
                                   const int guest_index) {
  return FlagMetrics{
      .cpus = parsed_flags.cpus.ForIndex(guest_index),
      .daemon = parsed_flags.daemon.ForIndex(guest_index),
      .data_policy = parsed_flags.data_policy.ForIndex(guest_index),
      // the same extra_kernel_cmdline value is used for all guests
      .extra_kernel_cmdline = parsed_flags.extra_kernel_cmdline.ForIndex(0),
      .gpu_mode = parsed_flags.gpu_mode.ForIndex(guest_index),
      .guest_enforce_security =
          parsed_flags.guest_enforce_security.ForIndex(guest_index),
      .memory_mb = parsed_flags.memory_mb.ForIndex(guest_index),
      .restart_subprocesses =
          parsed_flags.restart_subprocesses.ForIndex(guest_index),
      .system_image_dir_specified = !parsed_flags.system_image_dir.IsDefault(),
  };
}

}  // namespace cuttlefish
