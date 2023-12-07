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

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/run_cvd_proc_collector.h"

namespace cuttlefish {

class RunCvdProcessManager {
 public:
  using GroupProcInfo = RunCvdProcessCollector::GroupProcInfo;

  static Result<RunCvdProcessManager> Get();
  // called by cvd reset handler
  Result<void> KillAllCuttlefishInstances(bool cvd_server_children_only,
                                          bool clear_runtime_dirs);
  // called by cvd start
  Result<void> ForcefullyStopGroup(bool cvd_server_children_only,
                                   uid_t any_id_in_group);

 private:
  RunCvdProcessManager() = delete;
  RunCvdProcessManager(RunCvdProcessCollector&&);
  static Result<void> RunStopCvd(const GroupProcInfo& run_cvd_info,
                                 bool clear_runtime_dirs);
  Result<void> RunStopCvdAll(bool cvd_server_children_only,
                             bool clear_runtime_dirs);
  Result<void> SendSignal(bool cvd_server_children_only, const GroupProcInfo&);
  Result<void> DeleteLockFile(bool cvd_server_children_only,
                              const GroupProcInfo&);
  Result<void> ForcefullyStopGroup(bool cvd_server_children_only,
                                   const GroupProcInfo& group);

  RunCvdProcessCollector run_cvd_process_collector_;
};

struct DeviceClearOptions {
  bool cvd_server_children_only;
  bool clear_instance_dirs;
};

/*
 * Runs stop_cvd for all cuttlefish instances found based on run_cvd processes,
 * and send SIGKILL to the run_cvd processes.
 *
 * If cvd_server_children_only is set, it kills the run_cvd processes that were
 * started by a cvd server process.
 */
Result<void> KillAllCuttlefishInstances(const DeviceClearOptions& options);

Result<void> KillCvdServerProcess();

}  // namespace cuttlefish
