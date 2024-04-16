/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <android-base/logging.h>

#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/instance_manager.h"

namespace cuttlefish {

class Cvd {
 public:
  Cvd(const android::base::LogSeverity verbosity,
      InstanceLockFileManager& instance_lockfile_manager,
      InstanceManager& instance_manager,
      HostToolTargetManager& host_tool_target_manager, bool optout);

  Result<cvd::Response> HandleCommand(
      const std::vector<std::string>& cvd_process_args,
      const std::unordered_map<std::string, std::string>& env,
      const std::vector<std::string>& selector_args);

  Result<void> HandleCvdCommand(
      const std::vector<std::string>& all_args,
      const std::unordered_map<std::string, std::string>& env);

  Result<void> HandleAcloud(
    const std::vector<std::string>& args,
    const std::unordered_map<std::string, std::string>& env);

 private:
  InstanceLockFileManager& instance_lockfile_manager_;
  InstanceManager& instance_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  std::atomic<bool> optout_;
  android::base::LogSeverity verbosity_;
};

}  // namespace cuttlefish
