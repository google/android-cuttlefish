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

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class RunCvdProcessCollector {
 public:
  struct GroupProcInfo {
    std::string home_;
    std::string exec_path_;
    std::string stop_cvd_path_;
    bool is_cvd_server_started_;
    std::optional<std::string> android_host_out_;
    struct InstanceInfo {
      std::set<pid_t> pids_;
      cvd_common::Envs envs_;
      cvd_common::Args cmd_args_;
      unsigned id_;
    };
    // instance id to instance info mapping
    std::unordered_map<unsigned, InstanceInfo> instances_;
  };
  const std::vector<GroupProcInfo>& CfGroups() const { return cf_groups_; }

  static Result<RunCvdProcessCollector> Get();
  RunCvdProcessCollector(const RunCvdProcessCollector&) = delete;
  RunCvdProcessCollector(RunCvdProcessCollector&&) = default;

 private:
  RunCvdProcessCollector() = default;
  static Result<std::vector<GroupProcInfo>> CollectInfo();
  std::vector<GroupProcInfo> cf_groups_;
};

}  // namespace cuttlefish
