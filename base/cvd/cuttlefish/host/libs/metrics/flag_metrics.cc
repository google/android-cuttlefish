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

#include "cuttlefish/host/commands/assemble_cvd/flags/cpus.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/daemon.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/data_policy.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/extra_kernel_cmdline.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/gpu_mode.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/guest_enforce_security.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/memory_mb.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/restart_subprocesses.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::vector<FlagMetrics>> GetFlagMetrics(const int guest_count) {
  std::vector<FlagMetrics> result;
  result.reserve(guest_count);

  CpusFlag cpus = CF_EXPECT(CpusFlag::FromGlobalGflags());
  DaemonFlag daemon = CF_EXPECT(DaemonFlag::FromGlobalGflags());
  DataPolicyFlag data_policy = CF_EXPECT(DataPolicyFlag::FromGlobalGflags());
  ExtraKernelCmdlineFlag extra_kernel_cmdline =
      ExtraKernelCmdlineFlag::FromGlobalGflags();
  GpuModeFlag gpu_mode = CF_EXPECT(GpuModeFlag::FromGlobalGflags());
  GuestEnforceSecurityFlag guest_enforce_security =
      CF_EXPECT(GuestEnforceSecurityFlag::FromGlobalGflags());
  MemoryMbFlag memory_mb = CF_EXPECT(MemoryMbFlag::FromGlobalGflags());
  RestartSubprocessesFlag restart_subprocesses =
      CF_EXPECT(RestartSubprocessesFlag::FromGlobalGflags());
  SystemImageDirFlag system_image_dir =
      CF_EXPECT(SystemImageDirFlag::FromGlobalGflags());

  for (int i = 0; i < guest_count; i++) {
    result.emplace_back(FlagMetrics{
        .cpus = cpus.ForIndex(i),
        .daemon = daemon.ForIndex(i),
        .data_policy = data_policy.ForIndex(i),
        // the value is used for all guests
        .extra_kernel_cmdline = extra_kernel_cmdline.ForIndex(0),
        .gpu_mode = gpu_mode.ForIndex(i),
        .guest_enforce_security = guest_enforce_security.ForIndex(i),
        .memory_mb = memory_mb.ForIndex(i),
        .restart_subprocesses = restart_subprocesses.ForIndex(i),
        .system_image_dir_specified = !system_image_dir.IsDefault(),
    });
  }
  return result;
}

}  // namespace cuttlefish
