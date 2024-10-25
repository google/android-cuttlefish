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
#include "host/commands/cvd/instances/instance_lock.h"
#include "host/commands/cvd/cli/selector/creation_analyzer.h"
#include "host/commands/cvd/instances/group_selector.h"
#include "host/commands/cvd/instances/instance_database.h"
#include "host/commands/cvd/instances/instance_database_types.h"
#include "host/commands/cvd/instances/instance_group_record.h"
#include "host/commands/cvd/instances/instance_record.h"
#include "host/commands/cvd/instances/instance_selector.h"
#include "host/commands/cvd/cli/selector/selector_common_parser.h"

namespace cuttlefish {

class InstanceManager {
 public:
  using GroupCreationInfo = selector::GroupCreationInfo;
  using LocalInstance = selector::LocalInstance;
  using LocalInstanceGroup = selector::LocalInstanceGroup;
  using GroupSelector = selector::GroupSelector;
  using InstanceSelector = selector::InstanceSelector;
  using Queries = selector::Queries;
  using Query = selector::Query;
  template <typename T>
  using Set = selector::Set<T>;

  InstanceManager(InstanceLockFileManager&,
                  selector::InstanceDatabase& instance_db);

  // For cvd start
  Result<selector::CreationAnalyzer> CreationAnalyzer(
      const selector::CreationAnalyzer::CreationAnalyzerParam& param);

  Result<LocalInstanceGroup> SelectGroup(
      const selector::SelectorOptions& selector_options,
      const cvd_common::Envs& envs, const Queries& extra_queries = {});

  Result<std::pair<LocalInstance, LocalInstanceGroup>> SelectInstance(
      const selector::SelectorOptions& selector_options,
      const cvd_common::Envs& envs, const Queries& extra_queries = {});

  Result<bool> HasInstanceGroups();
  Result<LocalInstanceGroup> CreateInstanceGroup(
      const selector::GroupCreationInfo& group_info);
  Result<void> UpdateInstanceGroup(const LocalInstanceGroup& group);
  Result<bool> RemoveInstanceGroupByHome(const std::string&);

  cvd::Status CvdClear(const CommandRequest&);
  static Result<std::string> GetCuttlefishConfigPath(const std::string& home);

  Result<std::optional<InstanceLockFile>> TryAcquireLock(int instance_num);

  Result<std::vector<LocalInstanceGroup>> FindGroups(const Query& query) const;
  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const Queries& queries) const;
  Result<LocalInstanceGroup> FindGroup(const Query& query) const;
  Result<LocalInstanceGroup> FindGroup(const Queries& queries) const;

  Result<std::pair<LocalInstance, LocalInstanceGroup>> FindInstanceById(
      unsigned id) const;

  Result<void> SetAcloudTranslatorOptout(bool optout);
  Result<bool> GetAcloudTranslatorOptout() const;

  Result<void> IssueStopCommand(const CommandRequest& request,
                                const std::string& config_file_path,
                                LocalInstanceGroup& group);

 private:
  Result<std::string> StopBin(const std::string& host_android_out);

  InstanceLockFileManager& lock_manager_;
  selector::InstanceDatabase& instance_db_;
};

}  // namespace cuttlefish
