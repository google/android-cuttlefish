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

#include <mutex>
#include <string>
#include <unordered_map>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/flags_collector.h"
#include "host/commands/cvd/server_command/host_tool_target.h"

namespace cuttlefish {

struct HostToolFlagRequestForm {
  std::string artifacts_path;
  std::string start_bin;
  std::string flag_name;
};

class HostToolTargetManager {
 public:
  INJECT(HostToolTargetManager()) {}

  Result<FlagInfo> ReadFlag(const HostToolFlagRequestForm& request);

 private:
  using HostToolTargetMap = std::unordered_map<std::string, HostToolTarget>;
  // map from artifact dir to host tool target information object
  HostToolTargetMap host_target_table_;
  std::mutex table_mutex_;
};

}  // namespace cuttlefish
