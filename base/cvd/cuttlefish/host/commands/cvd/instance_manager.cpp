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

#include "host/commands/cvd/instance_manager.h"

#include <signal.h>

#include <sstream>

#include <android-base/file.h>
#include <android-base/scopeguard.h>
#include <fmt/format.h>
#include "json/json.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/libs/config/config_constants.h"
#include "host/libs/config/config_utils.h"

namespace cuttlefish {
namespace {

// Returns true only if command terminated normally, and returns 0
Result<void> RunCommand(Command&& command) {
  auto subprocess = command.Start();
  siginfo_t infop{};
  // This blocks until the process exits, but doesn't reap it.
  auto result = subprocess.Wait(&infop, WEXITED);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  CF_EXPECT(infop.si_code == CLD_EXITED && infop.si_status == 0);
  return {};
}

}  // namespace

Result<std::string> InstanceManager::GetCuttlefishConfigPath(
    const std::string& home) {
  return CF_EXPECT(selector::GetCuttlefishConfigPath(home));
}

InstanceManager::InstanceManager(
    InstanceLockFileManager& lock_manager,
    HostToolTargetManager& host_tool_target_manager,
    selector::InstanceDatabase& instance_db)
    : lock_manager_(lock_manager),
      host_tool_target_manager_(host_tool_target_manager),
      instance_db_(instance_db) {}

Result<void> InstanceManager::LoadFromJson(const Json::Value& db_json) {
  CF_EXPECT(instance_db_.LoadFromJson(db_json));
  return {};
}

Result<void> InstanceManager::SetAcloudTranslatorOptout(bool optout) {
  CF_EXPECT(instance_db_.SetAcloudTranslatorOptout(optout));
  return {};
}

Result<bool> InstanceManager::GetAcloudTranslatorOptout() const {
  return CF_EXPECT(instance_db_.GetAcloudTranslatorOptout());
}

Result<InstanceManager::GroupCreationInfo> InstanceManager::Analyze(
    const CreationAnalyzerParam& param) {
  return CF_EXPECT(CreationAnalyzer::Analyze(param, lock_manager_));
}

Result<InstanceManager::LocalInstanceGroup> InstanceManager::SelectGroup(
    const cvd_common::Args& selector_args, const cvd_common::Envs& envs,
    const Queries& extra_queries) {
  auto group_selector =
      CF_EXPECT(GroupSelector::GetSelector(selector_args, extra_queries, envs));
  return CF_EXPECT(group_selector.FindGroup(instance_db_));
}

Result<InstanceManager::LocalInstance> InstanceManager::SelectInstance(
    const cvd_common::Args& selector_args, const cvd_common::Envs& envs,
    const Queries& extra_queries) {
  auto instance_selector = CF_EXPECT(
      InstanceSelector::GetSelector(selector_args, extra_queries, envs));
  return CF_EXPECT(instance_selector.FindInstance(instance_db_));
}

Result<bool> InstanceManager::HasInstanceGroups() {
  return !CF_EXPECT(instance_db_.IsEmpty());
}

Result<void> InstanceManager::SetInstanceGroup(
    const selector::GroupCreationInfo& group_info) {
  const auto group_name = group_info.group_name;
  const auto home_dir = group_info.home;
  const auto host_artifacts_path = group_info.host_artifacts_path;
  const auto product_out_path = group_info.product_out_path;
  const auto& per_instance_info = group_info.instances;
  cvd::InstanceGroup new_group;
  new_group.set_name(group_name);
  new_group.set_home_directory(home_dir);
  new_group.set_host_artifacts_path(host_artifacts_path);
  new_group.set_product_out_path(product_out_path);
  new_group.set_start_time_sec(
      selector::CvdServerClock::to_time_t(selector::CvdServerClock::now()));
  for (const auto& instance : per_instance_info) {
    auto& new_instance = *new_group.add_instances();
    new_instance.set_id(instance.instance_id_);
    new_instance.set_name(instance.per_instance_name_);
    new_instance.set_state(cvd::INSTANCE_STATE_RUNNING);
  }
  CF_EXPECT(instance_db_.AddInstanceGroup(new_group));
  return {};
}

Result<InstanceManager::LocalInstanceGroup>
InstanceManager::CreateInstanceGroup(
    const selector::GroupCreationInfo& group_info) {
  cvd::InstanceGroup new_group;
  new_group.set_name(group_info.group_name.empty()
                         ? selector::GenDefaultGroupName()
                         : group_info.group_name);
  new_group.set_home_directory(group_info.home);
  new_group.set_host_artifacts_path(group_info.host_artifacts_path);
  new_group.set_product_out_path(group_info.product_out_path);
  for (const auto& instance : group_info.instances) {
    auto& new_instance = *new_group.add_instances();
    new_instance.set_id(instance.instance_id_);
    new_instance.set_name(instance.per_instance_name_);
  }
  return CF_EXPECT(instance_db_.AddInstanceGroup(new_group));
}

Result<bool> InstanceManager::RemoveInstanceGroup(const std::string& dir) {
  auto group = CF_EXPECT(instance_db_.FindGroup({selector::kHomeField, dir}));
  return CF_EXPECT(instance_db_.RemoveInstanceGroup(group.GroupName()));
}

Result<std::string> InstanceManager::StopBin(
    const std::string& host_android_out) {
  return CF_EXPECT(host_tool_target_manager_.ExecBaseName({
      .artifacts_path = host_android_out,
      .op = "stop",
  }));
}

Result<void> InstanceManager::UpdateInstanceGroup(const LocalInstanceGroup& group) {
  CF_EXPECT(instance_db_.UpdateInstanceGroup(group));
  return {};
}

Result<void> InstanceManager::UpdateInstance(const LocalInstance& instance) {
  CF_EXPECT(instance_db_.UpdateInstance(instance));
  return {};
}

Result<void> InstanceManager::IssueStopCommand(
    const SharedFD& out, const SharedFD& err,
    const std::string& config_file_path,
    const selector::LocalInstanceGroup& group) {
  const auto stop_bin = CF_EXPECT(StopBin(group.HostArtifactsPath()));
  Command command(group.HostArtifactsPath() + "/bin/" + stop_bin);
  command.AddParameter("--clear_instance_dirs");
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  auto wait_result = RunCommand(std::move(command));
  /**
   * --clear_instance_dirs may not be available for old branches. This causes
   * the stop_cvd to terminates with a non-zero exit code due to the parsing
   * error. Then, we will try to re-run it without the flag.
   */
  if (!wait_result.ok()) {
    std::stringstream error_msg;
    error_msg << stop_bin << " was executed internally, and failed. It might "
              << "be failing to parse the new --clear_instance_dirs. Will try "
              << "without the flag.\n";
    WriteAll(err, error_msg.str());
    Command no_clear_instance_dir_command(group.HostArtifactsPath() + "/bin/" +
                                          stop_bin);
    no_clear_instance_dir_command.RedirectStdIO(
        Subprocess::StdIOChannel::kStdOut, out);
    no_clear_instance_dir_command.RedirectStdIO(
        Subprocess::StdIOChannel::kStdErr, err);
    no_clear_instance_dir_command.AddEnvironmentVariable(
        kCuttlefishConfigEnvVarName, config_file_path);
    wait_result = RunCommand(std::move(no_clear_instance_dir_command));
  }

  if (!wait_result.ok()) {
    WriteAll(err,
             "Warning: error stopping instances for dir \"" + group.HomeDir() +
                 "\".\nThis can happen if instances are already stopped.\n");
  }
  for (const auto& instance : group.Instances()) {
    auto lock = lock_manager_.TryAcquireLock(instance.InstanceId());
    if (lock.ok() && (*lock)) {
      (*lock)->Status(InUseState::kNotInUse);
      continue;
    }
    WriteAll(err, "InstanceLockFileManager failed to acquire lock");
  }
  return {};
}

cvd::Status InstanceManager::CvdClear(const SharedFD& out,
                                      const SharedFD& err) {
  cvd::Status status;
  const std::string config_json_name = cpp_basename(GetGlobalConfigFileLink());
  auto instance_groups_res = instance_db_.Clear();
  if (!instance_groups_res.ok()) {
    WriteAll(err, fmt::format("Failed to clear instance database: {}",
                              instance_groups_res.error().Message()));
    status.set_code(cvd::Status::INTERNAL);
    return status;
  }
  auto instance_groups = *instance_groups_res;
  for (const auto& group : instance_groups) {
    auto config_path = selector::GetCuttlefishConfigPath(group.HomeDir());
    if (config_path.ok()) {
      auto stop_result = IssueStopCommand(out, err, *config_path, group);
      if (!stop_result.ok()) {
        LOG(ERROR) << stop_result.error().FormatForEnv();
      }
    }
    RemoveFile(group.HomeDir() + "/cuttlefish_runtime");
    RemoveFile(group.HomeDir() + config_json_name);
  }
  // TODO(kwstephenkim): we need a better mechanism to make sure that
  // we clear all run_cvd processes.
  WriteAll(err, "Stopped all known instances\n");
  status.set_code(cvd::Status::OK);
  return status;
}

Result<std::optional<InstanceLockFile>> InstanceManager::TryAcquireLock(
    int instance_num) {
  return CF_EXPECT(lock_manager_.TryAcquireLock(instance_num));
}

Result<std::vector<InstanceManager::LocalInstanceGroup>>
InstanceManager::FindGroups(const Query& query) const {
  return CF_EXPECT(FindGroups(Queries{query}));
}

Result<std::vector<InstanceManager::LocalInstanceGroup>>
InstanceManager::FindGroups(const Queries& queries) const {
  return instance_db_.FindGroups(queries);
}

Result<std::vector<InstanceManager::LocalInstance>>
InstanceManager::FindInstances(const Query& query) const {
  return CF_EXPECT(FindInstances(Queries{query}));
}

Result<std::vector<InstanceManager::LocalInstance>>
InstanceManager::FindInstances(const Queries& queries) const {
  return instance_db_.FindInstances(queries);
}

Result<InstanceManager::LocalInstanceGroup> InstanceManager::FindGroup(
    const Query& query) const {
  return CF_EXPECT(FindGroup(Queries{query}));
}

Result<InstanceManager::LocalInstanceGroup> InstanceManager::FindGroup(
    const Queries& queries) const {
  auto output = CF_EXPECT(instance_db_.FindGroups(queries));
  CF_EXPECT_EQ(output.size(), 1ul);
  return *(output.begin());
}

Result<InstanceManager::UserGroupSelectionSummary>
InstanceManager::GroupSummaryMenu() const {
  UserGroupSelectionSummary summary;

  // List of Cuttlefish Instance Groups:
  //   [i] : group_name (created: TIME)
  //      <a> instance0.device_name() (id: instance_id)
  //      <b> instance1.device_name() (id: instance_id)
  std::stringstream ss;
  ss << "List of Cuttlefish Instance Groups:" << std::endl;
  int group_idx = 0;
  for (const auto& group : CF_EXPECT(instance_db_.InstanceGroups())) {
    fmt::print(ss, "  [{}] : {} (created: {})\n", group_idx, group.GroupName(),
               selector::Format(group.StartTime()));
    summary.idx_to_group_name[group_idx] = group.GroupName();
    char instance_idx = 'a';
    for (const auto& instance : group.Instances()) {
      fmt::print(ss, "    <{}> {} (id : {})\n", instance_idx++,
                 instance.DeviceName(), instance.InstanceId());
    }
    group_idx++;
  }
  summary.menu = ss.str();
  return summary;
}

}  // namespace cuttlefish
