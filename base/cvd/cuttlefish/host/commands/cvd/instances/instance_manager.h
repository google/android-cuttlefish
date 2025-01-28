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

#include <sys/types.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/selector/creation_analyzer.h"
#include "host/commands/cvd/instances/instance_database.h"
#include "host/commands/cvd/instances/instance_group_record.h"
#include "host/commands/cvd/instances/instance_record.h"
#include "host/commands/cvd/instances/lock/instance_lock.h"

namespace cuttlefish {

class InstanceManager {
 public:
  using GroupCreationInfo = selector::GroupCreationInfo;

  InstanceManager(InstanceLockFileManager&, InstanceDatabase& instance_db);

  // For cvd start
  Result<selector::CreationAnalyzer> CreationAnalyzer(
      const selector::CreationAnalyzer::CreationAnalyzerParam& param);

  Result<bool> HasInstanceGroups() const;
  Result<LocalInstanceGroup> CreateInstanceGroup(
      const selector::GroupCreationInfo& group_info);
  Result<void> UpdateInstanceGroup(const LocalInstanceGroup& group);
  Result<bool> RemoveInstanceGroupByHome(const std::string&);

  cvd::Status CvdClear(const CommandRequest&);

  Result<std::optional<InstanceLockFile>> TryAcquireLock(int instance_num);

  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const InstanceDatabase::Filter& filter) const;
  Result<LocalInstanceGroup> FindGroup(
      const InstanceDatabase::Filter& filter) const;

  Result<std::pair<LocalInstance, LocalInstanceGroup>> FindInstanceWithGroup(
      const InstanceDatabase::Filter& filter) const;

  Result<void> SetAcloudTranslatorOptout(bool optout);
  Result<bool> GetAcloudTranslatorOptout() const;

  Result<void> IssueStopCommand(const CommandRequest& request,
                                const std::string& config_file_path,
                                LocalInstanceGroup& group);

 private:
  Result<std::string> StopBin(const std::string& host_android_out);

  InstanceLockFileManager& lock_manager_;
  InstanceDatabase& instance_db_;
};

}  // namespace cuttlefish
