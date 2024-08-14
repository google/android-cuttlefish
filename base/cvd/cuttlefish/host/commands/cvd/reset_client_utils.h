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

#include <sys/types.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/run_cvd_proc_collector.h"

namespace cuttlefish {

class RunCvdProcessManager {
 public:
  using GroupProcInfo = RunCvdProcessCollector::GroupProcInfo;

  static Result<RunCvdProcessManager> Get();
  // called by cvd reset handler
  Result<void> KillAllCuttlefishInstances(bool clear_runtime_dirs);
  // called by cvd start
  Result<void> ForcefullyStopGroup(uid_t any_id_in_group);

 private:
  RunCvdProcessManager() = delete;
  RunCvdProcessManager(RunCvdProcessCollector&&);
  static Result<void> RunStopCvd(const GroupProcInfo& run_cvd_info,
                                 bool clear_runtime_dirs);
  Result<void> RunStopCvdAll(bool clear_runtime_dirs);
  Result<void> SendSignal(const GroupProcInfo&);
  Result<void> DeleteLockFile(const GroupProcInfo&);
  Result<void> ForcefullyStopGroup(const GroupProcInfo& group);

  RunCvdProcessCollector run_cvd_process_collector_;
};

/*
 * Runs stop_cvd for all cuttlefish instances found based on run_cvd processes,
 * and send SIGKILL to the run_cvd processes.
 */
Result<void> KillAllCuttlefishInstances(bool clear_runtime_dirs);

Result<void> KillCvdServerProcess();

}  // namespace cuttlefish
