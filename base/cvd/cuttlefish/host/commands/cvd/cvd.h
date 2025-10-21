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

#include <string>
#include <unordered_map>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"

namespace cuttlefish {

class Cvd {
 public:
  Cvd(InstanceManager&, InstanceLockFileManager&);

  Result<void> HandleCommand(
      const std::vector<std::string>& cvd_process_args,
      const std::unordered_map<std::string, std::string>& env,
      const std::vector<std::string>& selector_args);

  Result<void> HandleCvdCommand(
      const std::vector<std::string>& all_args,
      const std::unordered_map<std::string, std::string>& env);

 private:
  InstanceManager& instance_manager_;
  InstanceLockFileManager& lock_file_manager_;
};

}  // namespace cuttlefish
