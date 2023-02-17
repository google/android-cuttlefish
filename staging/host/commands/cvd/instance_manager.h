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

#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/selector/creation_analyzer.h"
#include "host/commands/cvd/selector/group_selector.h"
#include "host/commands/cvd/selector/instance_database.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"

namespace cuttlefish {

class InstanceManager {
 public:
  using CreationAnalyzer = selector::CreationAnalyzer;
  using CreationAnalyzerParam = CreationAnalyzer::CreationAnalyzerParam;
  using GroupCreationInfo = selector::GroupCreationInfo;
  using LocalInstanceGroup = selector::LocalInstanceGroup;
  using GroupSelector = selector::GroupSelector;

  using InstanceGroupDir = std::string;
  struct InstanceGroupInfo {
    std::string host_artifacts_path;
    std::set<int> instances;
  };

  INJECT(InstanceManager(InstanceLockFileManager&, HostToolTargetManager&));

  // For cvd start
  Result<GroupCreationInfo> Analyze(const std::string& sub_cmd,
                                    const CreationAnalyzerParam& param,
                                    const ucred& credential);

  Result<LocalInstanceGroup> SelectGroup(const cvd_common::Args& selector_args,
                                         const cvd_common::Envs& envs,
                                         const uid_t uid);

  bool HasInstanceGroups(const uid_t uid);
  Result<void> SetInstanceGroup(const uid_t uid,
                                const selector::GroupCreationInfo& group_info);
  void RemoveInstanceGroup(const uid_t uid, const InstanceGroupDir&);
  Result<InstanceGroupInfo> GetInstanceGroupInfo(const uid_t uid,
                                                 const InstanceGroupDir&);

  cvd::Status CvdClear(const SharedFD& out, const SharedFD& err);
  Result<cvd::Status> CvdFleet(const uid_t uid, const SharedFD& out,
                               const SharedFD& err,
                               const std::vector<std::string>& fleet_cmd_args);
  static Result<std::string> GetCuttlefishConfigPath(const std::string& home);

 private:
  Result<cvd::Status> CvdFleetImpl(const uid_t uid, const SharedFD& out,
                                   const SharedFD& err);
  Result<void> IssueStopCommand(const SharedFD& out, const SharedFD& err,
                                const std::string& config_file_path,
                                const selector::LocalInstanceGroup& group);
  Result<std::string> StopBin(const std::string& host_android_out);

  selector::InstanceDatabase& GetInstanceDB(const uid_t uid);
  InstanceLockFileManager& lock_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  mutable std::mutex instance_db_mutex_;
  std::unordered_map<uid_t, selector::InstanceDatabase> instance_dbs_;

  using Query = selector::Query;
};

}  // namespace cuttlefish
