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
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/selector/creation_analyzer.h"
#include "host/commands/cvd/selector/instance_database.h"

namespace cuttlefish {

constexpr char kStatusBin[] = "cvd_internal_status";
constexpr char kStopBin[] = "cvd_internal_stop";

class InstanceManager {
 public:
  using CreationAnalyzer = selector::CreationAnalyzer;
  using CreationAnalyzerParam = CreationAnalyzer::CreationAnalyzerParam;
  using GroupCreationInfo = selector::GroupCreationInfo;

  using InstanceGroupDir = std::string;
  struct InstanceGroupInfo {
    std::string host_binaries_dir;
    std::set<int> instances;
  };

  INJECT(InstanceManager(InstanceLockFileManager&));

  Result<GroupCreationInfo> Analyze(const std::string& sub_cmd,
                                    const CreationAnalyzerParam& param,
                                    const std::optional<ucred>& credential);

  bool HasInstanceGroups(const uid_t uid);
  Result<void> SetInstanceGroup(const uid_t uid, const InstanceGroupDir&,
                                const InstanceGroupInfo&);
  void RemoveInstanceGroup(const uid_t uid, const InstanceGroupDir&);
  Result<InstanceGroupInfo> GetInstanceGroupInfo(const uid_t uid,
                                                 const InstanceGroupDir&);

  cvd::Status CvdClear(const uid_t uid, const SharedFD& out,
                       const SharedFD& err);
  Result<cvd::Status> CvdFleet(const uid_t uid, const SharedFD& out,
                               const SharedFD& err,
                               const std::optional<std::string>& env_config,
                               const std::string& host_tool_dir,
                               const std::vector<std::string>& args);
  static Result<std::string> GetCuttlefishConfigPath(const std::string& home);

 private:
  Result<cvd::Status> CvdFleetImpl(
      const uid_t uid, const SharedFD& out, const SharedFD& err,
      const std::optional<std::string>& env_config);
  Result<cvd::Status> CvdFleetHelp(const SharedFD& out, const SharedFD& err,
                                   const std::string& host_tool_dir);

  static void IssueStatusCommand(const SharedFD& out, const SharedFD& err,
                                 const std::string& config_file_path,
                                 const selector::LocalInstanceGroup& group);
  void IssueStopCommand(const SharedFD& out, const SharedFD& err,
                        const std::string& config_file_path,
                        const selector::LocalInstanceGroup& group);

  selector::InstanceDatabase& GetInstanceDB(const uid_t uid);
  InstanceLockFileManager& lock_manager_;

  mutable std::mutex instance_db_mutex_;
  std::unordered_map<uid_t, selector::InstanceDatabase> instance_dbs_;

  using Query = selector::Query;
};

}  // namespace cuttlefish
