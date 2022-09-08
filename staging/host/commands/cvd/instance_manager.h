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

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_lock.h"

namespace cuttlefish {

constexpr char kStatusBin[] = "cvd_internal_status";
constexpr char kStopBin[] = "cvd_internal_stop";

class InstanceManager {
 public:
  using InstanceGroupDir = std::string;
  struct InstanceGroupInfo {
    std::string host_binaries_dir;
    std::set<int> instances;
  };

  INJECT(InstanceManager(InstanceLockFileManager&));

  bool HasInstanceGroups() const;
  void SetInstanceGroup(const InstanceGroupDir&, const InstanceGroupInfo&);
  void RemoveInstanceGroup(const InstanceGroupDir&);
  Result<InstanceGroupInfo> GetInstanceGroup(const InstanceGroupDir&) const;

  cvd::Status CvdClear(const SharedFD& out, const SharedFD& err);
  cvd::Status CvdFleet(const SharedFD& out, const SharedFD& err,
                       const std::optional<std::string>& env_config,
                       const std::string& host_tool_dir,
                       const std::vector<std::string>& args) const;

 private:
  cvd::Status CvdFleetImpl(const SharedFD& out,
                           const std::optional<std::string>& env_config) const;
  cvd::Status CvdFleetHelp(const SharedFD& out, const SharedFD& err,
                           const std::string& host_tool_dir) const;

  InstanceLockFileManager& lock_manager_;

  mutable std::mutex instance_groups_mutex_;
  std::map<InstanceGroupDir, InstanceGroupInfo> instance_groups_;
};

Result<std::string> GetCuttlefishConfigPath(const std::string& assembly_dir);

}  // namespace cuttlefish
