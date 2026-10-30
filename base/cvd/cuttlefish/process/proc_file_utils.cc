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

#include "cuttlefish/process/proc_file_utils.h"

#include <sys/types.h>

#include <string>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::vector<pid_t>> CollectPidsByExecPath(const std::string& exec_path,
                                                 const uid_t uid) {
  auto input_pids = CF_EXPECT(CollectPids(uid));
  std::vector<pid_t> output_pids;
  for (const auto pid : input_pids) {
    auto pid_exec_path = GetExecutablePath(pid);
    if (!pid_exec_path.has_value()) {
      continue;
    }
    if (*pid_exec_path == exec_path) {
      output_pids.push_back(pid);
    }
  }
  return output_pids;
}

Result<std::vector<pid_t>> CollectPidsByArgv0(const std::string& expected_argv0,
                                              const uid_t uid) {
  auto input_pids = CF_EXPECT(CollectPids(uid));
  std::vector<pid_t> output_pids;
  for (const auto pid : input_pids) {
    auto argv_result = GetCmdArgs(pid);
    if (!argv_result.has_value()) {
      continue;
    }
    if (argv_result->empty()) {
      continue;
    }
    if (argv_result->front() == expected_argv0) {
      output_pids.push_back(pid);
    }
  }
  return output_pids;
}

}  // namespace cuttlefish
