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

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/flags_collector.h"
#include "host/commands/cvd/server_command/host_tool_target.h"
#include "host/commands/cvd/server_command/operation_to_bins_map.h"

namespace cuttlefish {

struct HostToolFlagRequestForm {
  std::string artifacts_path;
  // operations like stop, start, status, etc
  std::string op;
  std::string flag_name;
};

struct HostToolExecNameRequestForm {
  std::string artifacts_path;
  // operations like stop, start, status, etc
  std::string op;
};

class HostToolTargetManager {
 public:
  virtual ~HostToolTargetManager() = default;
  virtual Result<FlagInfo> ReadFlag(const HostToolFlagRequestForm& request) = 0;
  virtual Result<std::string> ExecBaseName(
      const HostToolExecNameRequestForm& request) = 0;
};

fruit::Component<fruit::Required<OperationToBinsMap>, HostToolTargetManager>
HostToolTargetManagerComponent();

}  // namespace cuttlefish
