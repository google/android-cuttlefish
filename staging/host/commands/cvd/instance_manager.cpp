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

#include <map>
#include <mutex>
#include <sstream>

#include <android-base/file.h>
#include <android-base/scopeguard.h>
#include <fmt/format.h>
#include <json/value.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

// Returns true only if command terminated normally, and returns 0
Result<void> RunCommand(Command&& command) {
  auto subprocess = std::move(command.Start());
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
  return selector::GetCuttlefishConfigPath(home);
}

InstanceManager::InstanceManager(
    InstanceLockFileManager& lock_manager,
    HostToolTargetManager& host_tool_target_manager)
    : lock_manager_(lock_manager),
      host_tool_target_manager_(host_tool_target_manager) {}

selector::InstanceDatabase& InstanceManager::GetInstanceDB(const uid_t uid) {
  if (!Contains(instance_dbs_, uid)) {
    instance_dbs_.try_emplace(uid);
  }
  return instance_dbs_[uid];
}

Result<Json::Value> InstanceManager::Serialize(const uid_t uid) {
  std::lock_guard lock(instance_db_mutex_);
  const auto& db = GetInstanceDB(uid);
  return db.Serialize();
}

Result<void> InstanceManager::LoadFromJson(const uid_t uid,
                                           const Json::Value& db_json) {
  std::lock_guard lock(instance_db_mutex_);
  CF_EXPECT(!Contains(instance_dbs_, uid));
  auto& db = GetInstanceDB(uid);
  CF_EXPECT(db.LoadFromJson(db_json));
  return {};
}

Result<InstanceManager::GroupCreationInfo> InstanceManager::Analyze(
    const std::string& sub_cmd, const CreationAnalyzerParam& param,
    const ucred& credential) {
  const uid_t uid = credential.uid;
  std::unique_lock lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  lock.unlock();

  auto group_creation_info = CF_EXPECT(CreationAnalyzer::Analyze(
      sub_cmd, param, credential, instance_db, lock_manager_));
  return {group_creation_info};
}

Result<InstanceManager::LocalInstanceGroup> InstanceManager::SelectGroup(
    const cvd_common::Args& selector_args, const cvd_common::Envs& envs,
    const uid_t uid) {
  return SelectGroup(selector_args, {}, envs, uid);
}

Result<InstanceManager::LocalInstanceGroup> InstanceManager::SelectGroup(
    const cvd_common::Args& selector_args, const Queries& extra_queries,
    const cvd_common::Envs& envs, const uid_t uid) {
  std::unique_lock lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  auto group_selector = CF_EXPECT(
      GroupSelector::GetSelector(selector_args, extra_queries, envs, uid));
  auto group = CF_EXPECT(group_selector.FindGroup(instance_db));
  return group;
}

Result<InstanceManager::LocalInstance::Copy> InstanceManager::SelectInstance(
    const cvd_common::Args& selector_args, const cvd_common::Envs& envs,
    const uid_t uid) {
  return SelectInstance(selector_args, {}, envs, uid);
}

Result<InstanceManager::LocalInstance::Copy> InstanceManager::SelectInstance(
    const cvd_common::Args& selector_args, const Queries& extra_queries,
    const cvd_common::Envs& envs, const uid_t uid) {
  std::unique_lock lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  auto instance_selector = CF_EXPECT(
      InstanceSelector::GetSelector(selector_args, extra_queries, envs, uid));
  auto instance_copy = CF_EXPECT(instance_selector.FindInstance(instance_db));
  return instance_copy;
}

bool InstanceManager::HasInstanceGroups(const uid_t uid) {
  std::lock_guard lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  return !instance_db.IsEmpty();
}

Result<void> InstanceManager::SetInstanceGroup(
    const uid_t uid, const selector::GroupCreationInfo& group_info) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);

  const auto group_name = group_info.group_name;
  const auto home_dir = group_info.home;
  const auto host_artifacts_path = group_info.host_artifacts_path;
  const auto product_out_path = group_info.product_out_path;
  const auto& per_instance_info = group_info.instances;

  auto new_group = CF_EXPECT(
      instance_db.AddInstanceGroup({.group_name = group_name,
                                    .home_dir = home_dir,
                                    .host_artifacts_path = host_artifacts_path,
                                    .product_out_path = product_out_path}));

  using InstanceInfo = selector::InstanceDatabase::InstanceInfo;
  std::vector<InstanceInfo> instances_info;
  for (const auto& instance : per_instance_info) {
    InstanceInfo info{.id = instance.instance_id_,
                      .name = instance.per_instance_name_};
    instances_info.push_back(info);
  }
  android::base::ScopeGuard action_on_failure([&instance_db, &new_group]() {
    /*
     * The way InstanceManager uses the database is that it adds an empty
     * group, gets an handle, and add instances to it. Thus, failing to adding
     * an instance to the group does not always mean that the instance group
     * addition fails. It is up to the caller. In this case, however, failing
     * to add an instance to a new group means failing to create an instance
     * group itself. Thus, we should remove the new instance group from the
     * database.
     *
     */
    instance_db.RemoveInstanceGroup(new_group.Get());
  });
  CF_EXPECTF(instance_db.AddInstances(group_name, instances_info),
             "Failed to add instances to the group \"{}\" so the group "
             "is not added",
             group_name);
  action_on_failure.Disable();
  return {};
}

void InstanceManager::RemoveInstanceGroup(const uid_t uid,
                                          const std::string& dir) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  auto result = instance_db.FindGroup({selector::kHomeField, dir});
  if (!result.ok()) return;
  auto group = *result;
  instance_db.RemoveInstanceGroup(group);
}

Result<std::string> InstanceManager::StopBin(
    const std::string& host_android_out) {
  const auto stop_bin = CF_EXPECT(host_tool_target_manager_.ExecBaseName({
      .artifacts_path = host_android_out,
      .op = "stop",
  }));
  return stop_bin;
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
    auto lock = lock_manager_.TryAcquireLock(instance->InstanceId());
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
  std::lock_guard lock(instance_db_mutex_);
  cvd::Status status;
  const std::string config_json_name = cpp_basename(GetGlobalConfigFileLink());
  for (auto& [uid, instance_db] : instance_dbs_) {
    auto&& instance_groups = instance_db.InstanceGroups();
    for (const auto& group : instance_groups) {
      auto config_path = group->GetCuttlefishConfigPath();
      if (config_path.ok()) {
        auto stop_result = IssueStopCommand(out, err, *config_path, *group);
        if (!stop_result.ok()) {
          LOG(ERROR) << stop_result.error().FormatForEnv();
        }
      }
      RemoveFile(group->HomeDir() + "/cuttlefish_runtime");
      RemoveFile(group->HomeDir() + config_json_name);
    }
    instance_db.Clear();
  }
  // TODO(kwstephenkim): we need a better mechanism to make sure that
  // we clear all run_cvd processes.
  instance_dbs_.clear();
  WriteAll(err, "Stopped all known instances\n");
  status.set_code(cvd::Status::OK);
  return status;
}

Result<std::optional<InstanceLockFile>> InstanceManager::TryAcquireLock(
    int instance_num) {
  std::lock_guard lock(instance_db_mutex_);
  return CF_EXPECT(lock_manager_.TryAcquireLock(instance_num));
}

Result<std::vector<InstanceManager::LocalInstanceGroup>>
InstanceManager::FindGroups(const uid_t uid, const Query& query) const {
  return CF_EXPECT(FindGroups(uid, Queries{query}));
}

Result<std::vector<InstanceManager::LocalInstanceGroup>>
InstanceManager::FindGroups(const uid_t uid, const Queries& queries) const {
  std::lock_guard lock(instance_db_mutex_);
  if (!Contains(instance_dbs_, uid)) {
    return {};
  }
  const auto& db = instance_dbs_.at(uid);
  auto groups = CF_EXPECT(db.FindGroups(queries));
  // create a copy as we are escaping the critical section
  std::vector<LocalInstanceGroup> output;
  for (const auto& group_ref : groups) {
    output.push_back(group_ref.Get());
  }
  return output;
}

Result<std::vector<InstanceManager::LocalInstance::Copy>>
InstanceManager::FindInstances(const uid_t uid, const Query& query) const {
  return CF_EXPECT(FindInstances(uid, Queries{query}));
}

Result<std::vector<InstanceManager::LocalInstance::Copy>>
InstanceManager::FindInstances(const uid_t uid, const Queries& queries) const {
  std::lock_guard lock(instance_db_mutex_);
  if (!Contains(instance_dbs_, uid)) {
    return {};
  }
  const auto& db = instance_dbs_.at(uid);
  auto instances = CF_EXPECT(db.FindInstances(queries));
  // create a copy as we are escaping the critical section
  std::vector<LocalInstance::Copy> output;
  for (const auto& instance : instances) {
    output.push_back(instance.Get().GetCopy());
  }
  return output;
}

Result<InstanceManager::LocalInstanceGroup> InstanceManager::FindGroup(
    const uid_t uid, const Query& query) const {
  return CF_EXPECT(FindGroup(uid, Queries{query}));
}

Result<InstanceManager::LocalInstanceGroup> InstanceManager::FindGroup(
    const uid_t uid, const Queries& queries) const {
  std::lock_guard lock(instance_db_mutex_);
  CF_EXPECT(Contains(instance_dbs_, uid));
  const auto& db = instance_dbs_.at(uid);
  auto output = CF_EXPECT(db.FindGroups(queries));
  CF_EXPECT_EQ(output.size(), 1);
  return *(output.begin());
}

Result<std::vector<std::string>> InstanceManager::AllGroupNames(
    const uid_t uid) const {
  std::lock_guard lock(instance_db_mutex_);
  CF_EXPECT(Contains(instance_dbs_, uid));
  const auto& db = instance_dbs_.at(uid);
  auto& local_instance_groups = db.InstanceGroups();
  std::vector<std::string> group_names;
  group_names.reserve(local_instance_groups.size());
  for (const auto& group : local_instance_groups) {
    group_names.push_back(group->GroupName());
  }
  return group_names;
}

}  // namespace cuttlefish
