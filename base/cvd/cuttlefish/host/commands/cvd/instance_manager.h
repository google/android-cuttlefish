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
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/command_request.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/selector/creation_analyzer.h"
#include "host/commands/cvd/selector/group_selector.h"
#include "host/commands/cvd/selector/instance_database.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_selector.h"
#include "host/commands/cvd/selector/selector_common_parser.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"

namespace cuttlefish {

class InstanceManager {
 public:
  using GroupCreationInfo = selector::GroupCreationInfo;
  using LocalInstanceGroup = selector::LocalInstanceGroup;
  using GroupSelector = selector::GroupSelector;
  using InstanceSelector = selector::InstanceSelector;
  using Queries = selector::Queries;
  using Query = selector::Query;
  template <typename T>
  using Set = selector::Set<T>;

  InstanceManager(InstanceLockFileManager&, HostToolTargetManager&,
                  selector::InstanceDatabase& instance_db);

  // For cvd start
  Result<selector::CreationAnalyzer> CreationAnalyzer(
      const selector::CreationAnalyzer::CreationAnalyzerParam& param);

  Result<LocalInstanceGroup> SelectGroup(
      const selector::SelectorOptions& selector_options,
      const cvd_common::Envs& envs, const Queries& extra_queries = {});

  Result<std::pair<cvd::Instance, LocalInstanceGroup>> SelectInstance(
      const selector::SelectorOptions& selector_options,
      const cvd_common::Envs& envs, const Queries& extra_queries = {});

  Result<bool> HasInstanceGroups();
  Result<LocalInstanceGroup> CreateInstanceGroup(
      const selector::GroupCreationInfo& group_info);
  Result<void> UpdateInstanceGroup(const LocalInstanceGroup& group);
  Result<void> UpdateInstance(const LocalInstanceGroup& group,
                              const cvd::Instance& instance);
  Result<bool> RemoveInstanceGroupByHome(const std::string&);

  cvd::Status CvdClear(const CommandRequest&);
  static Result<std::string> GetCuttlefishConfigPath(const std::string& home);

  Result<std::optional<InstanceLockFile>> TryAcquireLock(int instance_num);

  Result<std::vector<LocalInstanceGroup>> FindGroups(const Query& query) const;
  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const Queries& queries) const;
  Result<LocalInstanceGroup> FindGroup(const Query& query) const;
  Result<LocalInstanceGroup> FindGroup(const Queries& queries) const;
  Result<void> LoadFromJson(const Json::Value&);

  Result<void> SetAcloudTranslatorOptout(bool optout);
  Result<bool> GetAcloudTranslatorOptout() const;

  Result<void> IssueStopCommand(const CommandRequest& request,
                                const std::string& config_file_path,
                                selector::LocalInstanceGroup& group);

 private:
  Result<std::string> StopBin(const std::string& host_android_out);

  InstanceLockFileManager& lock_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  selector::InstanceDatabase& instance_db_;
};

}  // namespace cuttlefish
