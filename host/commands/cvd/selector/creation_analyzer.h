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

#include <sys/socket.h>  // for ucred

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/libs/utils/result.h"

#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/selector/instance_database.h"
#include "host/commands/cvd/selector/selector_cmdline_parser.h"
#include "host/commands/cvd/selector/unique_resource_allocator.h"

namespace cuttlefish {
namespace selector {

struct PerInstanceInfo {
  // for the sake of std::vector::emplace_back
  PerInstanceInfo(const unsigned id, const std::string& per_instance_name,
                  InstanceLockFile&& instance_file_lock)
      : instance_id_(id),
        per_instance_name_(per_instance_name),
        instance_file_lock_(std::move(instance_file_lock)) {}
  const unsigned instance_id_;
  const std::string per_instance_name_;
  InstanceLockFile instance_file_lock_;
};

/**
 * Creation is currently group by group
 *
 * If you want one instance, you should create a group with one instance.
 */
struct GroupCreationInfo {
  std::string home;
  std::string host_artifacts_path;  ///< e.g. out/host/linux-x86
  std::string group_name;
  std::vector<PerInstanceInfo> instances;
  std::vector<std::string> args;
  std::unordered_map<std::string, std::string> envs;
};

/**
 * Instance IDs:
 *  Use the InstanceNumCalculator's logic
 *
 * HOME directory:
 *  If given in envs and is different from the system-wide home, use it
 *  If not, try kParentOfDefaultHomeDirectories/.${group_name}
 *
 * host_artifacts_path:
 *  ANDROID_HOST_OUT must be given.
 *
 * Group name:
 *  if --group_name or --device_name is given, find the group name there
 *  if --name is given and when it is a group name (i.e. --name=<one token>
 *  and that one token is an eligible group name, and the operation is for
 *  a group -- e.g. start), use the "name" as a group name
 *  if a group name is not given, automatically generate:
 *   default_prefix + "_" + android::base::Join(instance_ids, "_")
 *
 * Per-instance name:
 *  When not given, use std::string(id) as the per instance name of each
 *
 * Number of instances:
 *  Controlled by --instance_nums, --num_instances, etc.
 *  Also controlled by --device_name or equivalent options
 *
 * p.s.
 *  dependency: (a-->b means b depends on a)
 *    group_name --> HOME
 *    instance ids --> per_instance_name
 *
 */
class CreationAnalyzer {
 public:
  struct CreationAnalyzerParam {
    const std::vector<std::string>& cmd_args;
    const std::unordered_map<std::string, std::string>& envs;
    const std::vector<std::string>& selector_args;
  };

  static Result<GroupCreationInfo> Analyze(
      const CreationAnalyzerParam& param,
      const std::optional<ucred>& credential,
      InstanceLockFileManager& instance_lock_file_manager);

 private:
  using IdAllocator = UniqueResourceAllocator<unsigned>;

  CreationAnalyzer(const CreationAnalyzerParam& param,
                   const std::optional<ucred>& credential,
                   SelectorFlagsParser&& selector_options_parser_,
                   InstanceLockFileManager& instance_lock_file_manager);

  Result<GroupCreationInfo> Analyze();

  /**
   * calculate n_instances_ and instance_ids_
   */
  Result<std::vector<PerInstanceInfo>> AnalyzeInstanceIdsWithLock();

  /*
   * When group name is nil, it is auto-generated using instance ids
   *
   * if the given ids are {l, m, n}, the auto-generated group name will be
   * GenDefaultGroupName() + "_l_m_n." If the ids set is equal to {1}, the
   * auto-generated group name will be just GenDefaultGroupName()
   *
   */
  std::string AnalyzeGroupName(const std::vector<PerInstanceInfo>&) const;

  /**
   * Figures out the HOME directory
   *
   *  If given in envs and is different from the system-wide home, use it
   *  If not, try $(SYSTEM_WIDE_HOME)/.cuttlefish_home/group_name
   *
   * The issue here is, mostly, HOME is given anyway. How would we tell
   * if the HOME is given explicitly or not?
   * e.g. HOME=/any/path cvd start vs. cvd start
   *
   */
  Result<std::string> AnalyzeHome() const;

  // inputs
  std::vector<std::string> cmd_args_;
  std::unordered_map<std::string, std::string> envs_;
  std::vector<std::string> selector_args_;
  const std::optional<ucred> credential_;

  // information to return later
  std::string home_;
  std::string host_artifacts_path_;  ///< e.g. out/host/linux-x86
  std::string group_name_;
  std::optional<std::vector<std::string>> per_instance_names_;

  // internal, temporary
  SelectorFlagsParser selector_options_parser_;
  InstanceLockFileManager& instance_file_lock_manager_;
};

}  // namespace selector
}  // namespace cuttlefish
