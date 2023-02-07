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

namespace cuttlefish {

Result<FlagInfo> HostToolTargetManager::ReadFlag(
    const HostToolFlagRequestForm& request) {
  std::lock_guard<std::mutex> lock(table_mutex_);
  if (!Contains(host_target_table_, request.artifacts_path) ||
      host_target_table_.at(request.artifacts_path).IsDirty()) {
    HostToolTarget new_host_tool_target = CF_EXPECT(
        HostToolTarget::Create(request.artifacts_path, request.start_bin));
    host_target_table_.emplace(request.artifacts_path,
                               std::move(new_host_tool_target));
  }
  const HostToolTarget& host_tool_target =
      host_target_table_.at(request.artifacts_path);
  auto flag_info = CF_EXPECT(host_tool_target.GetFlagInfo(request.flag_name));
  return flag_info;
}

}  // namespace cuttlefish
