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
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::vector<FlagMetrics>> GetFlagMetrics(const int guest_count) {
  std::vector<FlagMetrics> result;
  result.reserve(guest_count);

  CpusFlag cpus_values = CF_EXPECT(CpusFlag::FromGlobalGflags());
  DaemonFlag daemon_values = CF_EXPECT(DaemonFlag::FromGlobalGflags());
  DataPolicyFlag data_policy_values =
      CF_EXPECT(DataPolicyFlag::FromGlobalGflags());

  for (int i = 0; i < guest_count; i++) {
    result.emplace_back(FlagMetrics{
        .cpus = cpus_values.ForIndex(i),
        .daemon = daemon_values.ForIndex(i),
        .data_policy = data_policy_values.ForIndex(i),
    });
  }
  return result;
}

}  // namespace cuttlefish
