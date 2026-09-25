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

#include "cuttlefish/metrics/guest/parsed_flags.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_builds.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/find_builds.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/cpus.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/daemon.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/data_policy.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/extra_kernel_cmdline.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/gpu_mode.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/guest_enforce_security.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/memory_mb.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/restart_subprocesses.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/fetcher_configs.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<ParsedFlags> GetParsedFlags() {
  SystemImageDirFlag system_image_dir =
      CF_EXPECT(SystemImageDirFlag::FromGlobalGflags());
  FetcherConfigs fetcher_configs =
      FetcherConfigs::ReadFromDirectories(system_image_dir.AsVector());
  AndroidBuilds android_builds =
      CF_EXPECT(FindAndroidBuilds(system_image_dir, fetcher_configs));

  return ParsedFlags{
      .boot_image = CF_EXPECT(BootImageFlag::FromGlobalGflags(android_builds)),
      .cpus = CF_EXPECT(CpusFlag::FromGlobalGflags()),
      .daemon = CF_EXPECT(DaemonFlag::FromGlobalGflags()),
      .data_policy = CF_EXPECT(DataPolicyFlag::FromGlobalGflags()),
      .extra_kernel_cmdline = ExtraKernelCmdlineFlag::FromGlobalGflags(),
      .gpu_mode = CF_EXPECT(GpuModeFlag::FromGlobalGflags()),
      .guest_enforce_security =
          CF_EXPECT(GuestEnforceSecurityFlag::FromGlobalGflags()),
      .memory_mb = CF_EXPECT(MemoryMbFlag::FromGlobalGflags()),
      .restart_subprocesses =
          CF_EXPECT(RestartSubprocessesFlag::FromGlobalGflags()),
      .system_image_dir = system_image_dir,
  };
}

}  // namespace cuttlefish
