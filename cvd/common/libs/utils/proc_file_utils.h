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
#include <unistd.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "common/libs/utils/result.h"

/**
 * @file Utility functions to retrieve information from proc filesystem
 *
 * As of now, the major consumer is cvd.
 */
namespace cuttlefish {

static constexpr char kProcDir[] = "/proc";

// collects all pids whose owner is uid
Result<std::vector<pid_t>> CollectPids(const uid_t uid = getuid());

/* collects all pids that meet the following:
 *
 * 1. Belongs to the uid
 * 2. cpp_basename(`cat /proc/<pid>/cmdline`.front()) == exec_name
 * 3. cpp_basename(exec_name) == exec_name
 *
 */
Result<std::vector<pid_t>> CollectPidsByExecName(const std::string& exec_name,
                                                 const uid_t uid = getuid());

// "exec_path" is treated as an absolute path
Result<std::vector<pid_t>> CollectPidsByExecPath(const std::string& exec_path,
                                                 const uid_t uid = getuid());

Result<uid_t> OwnerUid(const pid_t pid);

// retrieves command line args for the pid
Result<std::vector<std::string>> GetCmdArgs(const pid_t pid);

// retrieves the path to the executable file used for the pid
Result<std::string> GetExecutablePath(const pid_t pid);

// retrieves the environment variables of the process, pid
Result<std::unordered_map<std::string, std::string>> GetEnvs(const pid_t pid);

}  // namespace cuttlefish
