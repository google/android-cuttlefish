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

#include <map>
#include <mutex>
#include <sstream>

#include <android-base/file.h>
#include <cvd_server.pb.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<std::string> InstanceManager::GetCuttlefishConfigPath(
    const std::string& home) {
  return selector::GetCuttlefishConfigPath(home);
}

InstanceManager::InstanceManager(InstanceLockFileManager& lock_manager)
    : lock_manager_(lock_manager) {}

selector::InstanceDatabase& InstanceManager::GetInstanceDB(const uid_t uid) {
  if (!Contains(instance_dbs_, uid)) {
    instance_dbs_.try_emplace(uid);
  }
  return instance_dbs_[uid];
}

Result<InstanceManager::GroupCreationInfo> InstanceManager::Analyze(
    const std::string& sub_cmd, const CreationAnalyzerParam& param,
    const ucred& credential) {
  const uid_t uid = credential.uid;
  auto& instance_db = GetInstanceDB(uid);

  auto group_creation_info = CF_EXPECT(CreationAnalyzer::Analyze(
      sub_cmd, param, credential, instance_db, lock_manager_));
  return {group_creation_info};
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
  const auto& per_instance_info = group_info.instances;

  auto new_group = CF_EXPECT(
      instance_db.AddInstanceGroup(group_name, home_dir, host_artifacts_path));

  for (const auto& instance : per_instance_info) {
    auto result = instance_db.AddInstance(
        new_group.Get(), instance.instance_id_, instance.per_instance_name_);
    if (!result.ok()) {
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
      CF_EXPECT(result.ok(), result.error().Trace());
    }
  }
  return {};
}

void InstanceManager::RemoveInstanceGroup(
    const uid_t uid, const InstanceManager::InstanceGroupDir& dir) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  auto result = instance_db.FindGroup({selector::kHomeField, dir});
  if (!result.ok()) return;
  auto group = *result;
  instance_db.RemoveInstanceGroup(group);
}

Result<InstanceManager::InstanceGroupInfo>
InstanceManager::GetInstanceGroupInfo(
    const uid_t uid, const InstanceManager::InstanceGroupDir& dir) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  auto group = CF_EXPECT(instance_db.FindGroup({selector::kHomeField, dir}));
  InstanceGroupInfo info;
  info.host_artifacts_path = group.Get().HostArtifactsPath();
  const auto& instances = group.Get().Instances();
  for (const auto& instance : instances) {
    CF_EXPECT(instance != nullptr);
    info.instances.insert(instance->InstanceId());
  }
  return {info};
}

struct StatusCommandOutput {
  std::string stderr_msg;
  Json::Value stdout_json;
};

static Result<StatusCommandOutput> IssueStatusCommand(
    const std::string& config_file_path,
    const selector::LocalInstanceGroup& group) {
  Command command(group.HostArtifactsPath() + "/bin/" + kStatusBin);
  command.AddParameter("--print");
  command.AddParameter("--all_instances");
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  StatusCommandOutput output;
  std::string stdout_buf;
  CF_EXPECT_EQ(RunWithManagedStdio(std::move(command), /* stdin */ nullptr,
                                   std::addressof(stdout_buf),
                                   std::addressof(output.stderr_msg)),
               0);
  if (stdout_buf.empty()) {
    Json::Reader().parse("{}", output.stdout_json);
    return output;
  }
  output.stdout_json = CF_EXPECT(ParseJson(stdout_buf));
  return output;
}

Result<cvd::Status> InstanceManager::CvdFleetImpl(const uid_t uid,
                                                  const SharedFD& out,
                                                  const SharedFD& err) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  const char _GroupDeviceInfoStart[] = "[\n";
  const char _GroupDeviceInfoSeparate[] = ",\n";
  const char _GroupDeviceInfoEnd[] = "]\n";
  WriteAll(out, _GroupDeviceInfoStart);
  auto&& instance_groups = instance_db.InstanceGroups();

  for (const auto& group : instance_groups) {
    CF_EXPECT(group != nullptr);
    auto config_path = group->GetCuttlefishConfigPath();
    if (config_path.ok()) {
      auto result = IssueStatusCommand(*config_path, *group);
      if (!result.ok()) {
        WriteAll(err, "      (unknown instance status error)");
      } else {
        const auto [stderr_msg, stdout_json] = *result;
        WriteAll(err, stderr_msg);
        // TODO(kwstephenkim): build a data structure that also includes
        // selector-related information, etc.
        WriteAll(out, stdout_json.toStyledString());
      }
      // move on
    } else {
      std::stringstream error_msg_stream;
      error_msg_stream << "The config file for \"group\" " << group->GroupName()
                       << " does not exist.\n";
      WriteAll(err, error_msg_stream.str());
    }
    if (group == *instance_groups.crbegin()) {
      continue;
    }
    WriteAll(out, _GroupDeviceInfoSeparate);
  }
  WriteAll(out, _GroupDeviceInfoEnd);
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

Result<cvd::Status> InstanceManager::CvdFleet(
    const uid_t uid, const SharedFD& out, const SharedFD& err,
    const std::vector<std::string>& fleet_cmd_args) {
  bool is_help = false;
  for (const auto& arg : fleet_cmd_args) {
    if (arg == "--help" || arg == "-help") {
      is_help = true;
      break;
    }
  }
  CF_EXPECT(!is_help,
            "cvd fleet --help should be handled by fleet handler itself.");
  const auto status = CF_EXPECT(CvdFleetImpl(uid, out, err));
  return status;
}

void InstanceManager::IssueStopCommand(
    const SharedFD& out, const SharedFD& err,
    const std::string& config_file_path,
    const selector::LocalInstanceGroup& group) {
  Command command(group.HostArtifactsPath() + "/bin/" + kStopBin);
  command.AddParameter("--clear_instance_dirs");
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  if (int wait_result = command.Start().Wait(); wait_result != 0) {
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
        IssueStopCommand(out, err, *config_path, *group);
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

}  // namespace cuttlefish
