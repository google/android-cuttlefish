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

#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/selector/creation_analyzer.h"
#include "host/commands/cvd/selector/group_selector.h"
#include "host/commands/cvd/selector/instance_database.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_selector.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"

namespace cuttlefish {

class InstanceManager {
 public:
  using CreationAnalyzer = selector::CreationAnalyzer;
  using CreationAnalyzerParam = CreationAnalyzer::CreationAnalyzerParam;
  using GroupCreationInfo = selector::GroupCreationInfo;
  using LocalInstanceGroup = selector::LocalInstanceGroup;
  using LocalInstance = selector::LocalInstance;
  using GroupSelector = selector::GroupSelector;
  using InstanceSelector = selector::InstanceSelector;
  using Queries = selector::Queries;
  using Query = selector::Query;
  template <typename T>
  using Set = selector::Set<T>;

  InstanceManager(InstanceLockFileManager&, HostToolTargetManager&);

  // For cvd start
  Result<GroupCreationInfo> Analyze(const std::string& sub_cmd,
                                    const CreationAnalyzerParam& param,
                                    const ucred& credential);

  Result<LocalInstanceGroup> SelectGroup(const cvd_common::Args& selector_args,
                                         const cvd_common::Envs& envsd);

  Result<LocalInstanceGroup> SelectGroup(const cvd_common::Args& selector_args,
                                         const Queries& extra_queries,
                                         const cvd_common::Envs& envs);

  Result<LocalInstance::Copy> SelectInstance(
      const cvd_common::Args& selector_args, const Queries& extra_queries,
      const cvd_common::Envs& envs);

  Result<LocalInstance::Copy> SelectInstance(
      const cvd_common::Args& selector_args, const cvd_common::Envs& envs);

  bool HasInstanceGroups();
  Result<void> SetInstanceGroup(const selector::GroupCreationInfo& group_info);
  void RemoveInstanceGroup(const std::string&);

  cvd::Status CvdClear(const SharedFD& out, const SharedFD& err);
  static Result<std::string> GetCuttlefishConfigPath(const std::string& home);

  Result<std::optional<InstanceLockFile>> TryAcquireLock(int instance_num);

  Result<std::vector<LocalInstanceGroup>> FindGroups(const Query& query) const;
  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const Queries& queries) const;
  Result<std::vector<LocalInstance::Copy>> FindInstances(
      const Query& query) const;
  Result<std::vector<LocalInstance::Copy>> FindInstances(
      const Queries& queries) const;

  Result<LocalInstanceGroup> FindGroup(const Query& query) const;
  Result<LocalInstanceGroup> FindGroup(const Queries& queries) const;
  Result<Json::Value> Serialize();
  Result<void> LoadFromJson(const Json::Value&);
  std::vector<std::string> AllGroupNames() const;

  struct UserGroupSelectionSummary {
    // Index to group name. This is the index printed in the menu
    // This field offers mapping between the number/index the user
    // selects and the group that is to be chosen
    std::unordered_map<int, std::string> idx_to_group_name;
    std::string menu;
  };
  Result<UserGroupSelectionSummary> GroupSummaryMenu() const;

 private:
  Result<void> IssueStopCommand(const SharedFD& out, const SharedFD& err,
                                const std::string& config_file_path,
                                const selector::LocalInstanceGroup& group);
  Result<std::string> StopBin(const std::string& host_android_out);

  InstanceLockFileManager& lock_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  mutable std::mutex instance_db_mutex_;
  selector::InstanceDatabase instance_db_;
};

}  // namespace cuttlefish
