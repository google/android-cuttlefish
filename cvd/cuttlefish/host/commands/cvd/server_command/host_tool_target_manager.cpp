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

#include "host/commands/cvd/server_command/host_tool_target_manager.h"

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "host/commands/cvd/common_utils.h"

namespace cuttlefish {

HostToolTargetManager::HostToolTargetManager() {
  using Candidates = std::vector<std::string>;
  op_to_possible_bins_map_["stop"] =
      Candidates{"cvd_internal_stop", "stop_cvd"};
  op_to_possible_bins_map_["start"] =
      Candidates{"cvd_internal_start", "launch_cvd"};
}

// use this only after acquiring the table_mutex_
Result<void> HostToolTargetManager::EnsureExistence(
    const std::string& artifacts_path) {
  if (!Contains(host_target_table_, artifacts_path)) {
    HostToolTarget new_host_tool_target = CF_EXPECT(
        HostToolTarget::Create(artifacts_path, op_to_possible_bins_map_));
    host_target_table_.emplace(artifacts_path, std::move(new_host_tool_target));
  }
  return {};
}

Result<void> HostToolTargetManager::UpdateOutdated(
    const std::string& artifacts_path) {
  CF_EXPECT(Contains(host_target_table_, artifacts_path));
  auto& host_target = host_target_table_.at(artifacts_path);
  if (!host_target.IsDirty()) {
    return {};
  }
  LOG(ERROR) << artifacts_path << " is new, so updating HostToolTarget";
  HostToolTarget new_host_tool_target = CF_EXPECT(
      HostToolTarget::Create(artifacts_path, op_to_possible_bins_map_));
  host_target_table_.emplace(artifacts_path, std::move(new_host_tool_target));
  return {};
}

Result<FlagInfo> HostToolTargetManager::ReadFlag(
    const HostToolFlagRequestForm& request) {
  std::lock_guard<std::mutex> lock(table_mutex_);
  CF_EXPECT(
      EnsureExistence(request.artifacts_path),
      "Could not create HostToolTarget object for " << request.artifacts_path);
  CF_EXPECT(UpdateOutdated(request.artifacts_path));
  auto& host_target = host_target_table_.at(request.artifacts_path);
  auto flag_info =
      CF_EXPECT(host_target.GetFlagInfo(HostToolTarget::FlagInfoRequest{
          .operation_ = request.op,
          .flag_name_ = request.flag_name,
      }));
  return flag_info;
}

}  // namespace cuttlefish
