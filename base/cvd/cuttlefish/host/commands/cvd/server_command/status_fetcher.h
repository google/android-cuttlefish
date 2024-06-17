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

#include <mutex>
#include <string>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"

namespace cuttlefish {

struct StatusFetcherOutput {
  std::string stderr_buf;
  Json::Value json_from_stdout;
  cvd::Response response;
};

class StatusFetcher {
 public:
  StatusFetcher(InstanceManager& instance_manager,
                HostToolTargetManager& host_tool_target_manager)
      : instance_manager_(instance_manager),
        host_tool_target_manager_(host_tool_target_manager) {}
  Result<void> Interrupt();
  Result<StatusFetcherOutput> FetchStatus(const RequestWithStdio&);

  Result<Json::Value> FetchGroupStatus(
      selector::LocalInstanceGroup& group,
      const RequestWithStdio& original_request);

 private:
  Result<std::string> GetBin(const std::string& host_artifacts_path) const;
  Result<StatusFetcherOutput> FetchOneInstanceStatus(
      const RequestWithStdio&, const InstanceManager::LocalInstanceGroup&,
      const std::string&, const unsigned);

  std::mutex interruptible_;
  bool interrupted_ = false;

  InstanceManager& instance_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  // needs to be exclusively owned by StatusFetcher
  SubprocessWaiter subprocess_waiter_;
};

}  // namespace cuttlefish
