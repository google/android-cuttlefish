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

#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "android-base/file.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/host/commands/cvd/instances/stop.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/metrics/device_metrics_orchestration.h"
#include "cuttlefish/posix/realpath.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/process/proc_file_utils.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr absl::string_view kGroupInProgressLockName = "in_progress.lock";

struct ActiveRunCvdProc {
  std::string home;
  std::optional<std::string> real_home;
  std::optional<unsigned> instance_id;
};

Result<std::vector<ActiveRunCvdProc>> CollectActiveRunCvdProcs() {
  std::vector<ActiveRunCvdProc> procs;
  std::vector<pid_t> pids =
      CF_EXPECT(CollectPidsByExecName("run_cvd", getuid()));
  for (pid_t pid : pids) {
    Result<std::unordered_map<std::string, std::string>> envs = GetEnvs(pid);
    if (!envs.has_value()) {
      continue;
    }
    auto home_it = envs->find("HOME");
    if (home_it == envs->end() || home_it->second.empty()) {
      continue;
    }
    ActiveRunCvdProc proc{
        .home = home_it->second,
    };
    if (Result<std::string> real_home = RealPath(proc.home);
        real_home.has_value()) {
      proc.real_home = std::move(*real_home);
    }
    if (auto id_it = envs->find(kCuttlefishInstanceEnvVarName);
        id_it != envs->end()) {
      unsigned parsed_id = 0;
      if (absl::SimpleAtoi(id_it->second, &parsed_id)) {
        proc.instance_id = parsed_id;
      }
    }
    procs.push_back(std::move(proc));
  }
  return procs;
}

bool MatchesActiveRunCvd(const LocalInstanceGroup& group,
                         const LocalInstance& instance,
                         const std::vector<ActiveRunCvdProc>& procs) {
  std::optional<std::string> real_group_home;
  if (Result<std::string> real_home = RealPath(group.HomeDir());
      real_home.has_value()) {
    real_group_home = std::move(*real_home);
  }
  for (const ActiveRunCvdProc& proc : procs) {
    bool home_matches =
        (proc.home == group.HomeDir()) ||
        (real_group_home.has_value() && proc.real_home.has_value() &&
         *real_group_home == *proc.real_home);
    if (!home_matches) {
      continue;
    }
    if (!proc.instance_id.has_value() || *proc.instance_id == instance.Id()) {
      return true;
    }
  }
  return false;
}

bool IsGroupBackingDirsIntact(const LocalInstanceGroup& group) {
  if (!DirectoryExists(group.HomeDir())) {
    return false;
  }
  if (!HostToolTarget(group.HostArtifactsPath())
           .GetStartBinName()
           .has_value()) {
    return false;
  }
  for (absl::string_view product_dir :
       absl::StrSplit(group.ProductOutPath(), ',')) {
    if (product_dir.empty() || !DirectoryExists(std::string(product_dir))) {
      return false;
    }
  }
  return true;
}

Result<std::unordered_map<unsigned, cvd::InstanceState>> ProbeGroupStateUpdates(
    const LocalInstanceGroup& group,
    std::optional<std::vector<ActiveRunCvdProc>>& cached_procs,
    bool caller_holds_in_progress_lock) {
  std::unordered_map<unsigned, cvd::InstanceState> updated_states;
  if (!caller_holds_in_progress_lock &&
      InstanceManager::IsGroupTransitionInProgress(group)) {
    return updated_states;
  }

  for (const LocalInstance& instance : group.Instances()) {
    const cvd::InstanceState state = instance.State();
    const bool check_dead = instance.IsActive();
    const bool check_alive = state == cvd::INSTANCE_STATE_BOOT_FAILED ||
                             state == cvd::INSTANCE_STATE_CANCELLED ||
                             state == cvd::INSTANCE_STATE_STOPPED;
    if (!check_dead && !check_alive) {
      continue;
    }
    const auto probe_start = std::chrono::steady_clock::now();
    bool is_alive =
        instance.CanConnectToLauncherMonitor(std::chrono::seconds(1));
    if (std::chrono::steady_clock::now() - probe_start >
        std::chrono::milliseconds(10)) {
      cached_procs.reset();
    }
    if (!is_alive && state != cvd::INSTANCE_STATE_STOPPED) {
      if (!cached_procs.has_value()) {
        cached_procs = CF_EXPECT(CollectActiveRunCvdProcs());
      }
      is_alive = MatchesActiveRunCvd(group, instance, *cached_procs);
    }
    if (check_dead && !is_alive) {
      updated_states[instance.Id()] = cvd::INSTANCE_STATE_STOPPED;
    } else if (check_alive && is_alive) {
      updated_states[instance.Id()] = cvd::INSTANCE_STATE_RUNNING;
    }
  }
  return updated_states;
}

Result<void> RefreshGroupWithProcs(
    LocalInstanceGroup& group,
    std::optional<std::vector<ActiveRunCvdProc>>& cached_procs,
    InstanceDatabase& instance_db, bool caller_holds_in_progress_lock) {
  if (caller_holds_in_progress_lock) {
    std::vector<LocalInstanceGroup> reloaded =
        CF_EXPECT(instance_db.FindGroups({.group_name = group.GroupName()}));
    CF_EXPECTF(
        reloaded.size() == 1 && reloaded.front().HomeDir() == group.HomeDir(),
        "Instance group '{}' no longer exists in database", group.GroupName());
    group = std::move(reloaded.front());
  }

  std::unordered_map<unsigned, cvd::InstanceState> updated_states =
      CF_EXPECT(ProbeGroupStateUpdates(group, cached_procs,
                                       caller_holds_in_progress_lock));
  if (updated_states.empty()) {
    return {};
  }

  std::function<bool()> precondition;
  if (!caller_holds_in_progress_lock) {
    precondition = [&group]() {
      return !InstanceManager::IsGroupTransitionInProgress(group);
    };
  }
  bool updated = CF_EXPECT(instance_db.CompareAndUpdateInstanceStates(
      group, updated_states, std::move(precondition)));
  if (updated) {
    for (LocalInstance& instance : group.Instances()) {
      if (auto it = updated_states.find(instance.Id());
          it != updated_states.end()) {
        instance.SetState(it->second);
      }
    }
  } else {
    Result<std::vector<LocalInstanceGroup>> reloaded =
        instance_db.FindGroups({.group_name = group.GroupName()});
    if (reloaded.has_value() && reloaded->size() == 1 &&
        reloaded->front().HomeDir() == group.HomeDir()) {
      group = std::move(reloaded->front());
    }
  }
  return {};
}

Result<void> RemoveGroupDirectory(const LocalInstanceGroup& group,
                                  const std::string& base_root_dir) {
  std::string prefix = absl::EndsWith(base_root_dir, "/")
                           ? base_root_dir
                           : absl::StrCat(base_root_dir, "/");
  if (!absl::StartsWith(group.HomeDir(), prefix)) {
    LOG(WARNING)
        << "Instance group home directory not under user specific directory("
        << base_root_dir << "), artifacts not deleted";
    return {};
  }
  std::string group_directory = CF_EXPECT(GroupDirFromHome(group.HomeDir()));
  if (DirectoryExists(group_directory, /*follow_symlinks=*/false)) {
    CF_EXPECT(RecursivelyRemoveDirectory(group_directory),
              "Failed to remove group directory");
  } else if (FileExists(group_directory, /*follow_symlinks=*/false)) {
    CF_EXPECT(RemoveFile(group_directory),
              "Failed to remove group directory symlink");
  }
  return {};
}

enum class ClaimBaseDirResult {
  kClaimed,
  kAlreadyExists,
};

Result<ClaimBaseDirResult> TryClaimGroupBaseDir(
    const std::optional<std::string>& custom_base_dir,
    const std::string& base_dir) {
  if (custom_base_dir.has_value()) {
    CF_EXPECT(DirectoryExists(*custom_base_dir));
    Result<void, int> res = Symlink(*custom_base_dir, base_dir);
    if (res.has_value()) {
      return ClaimBaseDirResult::kClaimed;
    }
    if (*res.error() == EEXIST) {
      return ClaimBaseDirResult::kAlreadyExists;
    }
    CF_EXPECT(std::move(res));
  }
  if (mkdir(base_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0) {
    return ClaimBaseDirResult::kClaimed;
  }
  if (errno == EEXIST) {
    return ClaimBaseDirResult::kAlreadyExists;
  }
  return CF_ERRNO("Failed to create group base directory '" << base_dir << "'");
}

Result<void> LinkOrMakeDir(std::optional<std::string> target,
                           const std::string& path) {
  if (target.has_value()) {
    CF_EXPECT(DirectoryExists(*target));
    CF_EXPECT(Symlink(*target, path));
  } else {
    CF_EXPECTF(EnsureDirectoryExists(path), "Failed to create directory: {}",
               path);
  }
  return {};
}

Result<void> PopulateGroupSubdirectories(
    const LocalInstanceGroup& group,
    InstanceManager::GroupDirectories directories) {
  CF_EXPECT(LinkOrMakeDir(std::move(directories.home), group.HomeDir()));
  CF_EXPECT(EnsureDirectoryExists(group.ArtifactsDir()));
  CF_EXPECT(LinkOrMakeDir(std::move(directories.host_artifacts_path),
                          group.HostArtifactsPath()));
  for (size_t i = 0; i < directories.product_out_paths.size(); ++i) {
    CF_EXPECT(LinkOrMakeDir(std::move(directories.product_out_paths[i]),
                            group.ProductDir(i)));
  }
  return {};
}

Result<std::string> StopBin(const std::string& host_android_out) {
  return CF_EXPECT(HostToolTarget(host_android_out).GetStopBinName());
}

}  // namespace

InstanceManager::InstanceManager(InstanceLockFileManager* lock_manager,
                                 InstanceDatabase& instance_db)
    : InstanceManager(lock_manager, instance_db, PerUserDir()) {}

InstanceManager::InstanceManager(InstanceLockFileManager* lock_manager,
                                 InstanceDatabase& instance_db,
                                 std::string base_root_dir)
    : lock_manager_(lock_manager),
      instance_db_(instance_db),
      base_root_dir_(std::move(base_root_dir)) {}

Result<Fd> InstanceManager::AcquireGroupInProgressLock(
    const LocalInstanceGroup& group) {
  CF_EXPECTF(DirectoryExists(group.BaseDir()),
             "Instance group base directory '{}' does not exist",
             group.BaseDir());
  std::string lock_path =
      fmt::format("{}/{}", group.BaseDir(), kGroupInProgressLockName);
  Fd fd = CF_EXPECT(Fd::Open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600));
  constexpr int kMaxLockRetries = 50;
  Result<void, int> flock_res = fd.Flock(LOCK_EX | LOCK_NB);
  for (int retry = 0; retry < kMaxLockRetries && !flock_res.has_value() &&
                      *flock_res.error() == EWOULDBLOCK;
       ++retry) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    flock_res = fd.Flock(LOCK_EX | LOCK_NB);
  }
  if (!flock_res.has_value() && *flock_res.error() == EWOULDBLOCK) {
    return CF_ERR(
        "Selected instance group is already starting, use `cvd create` to "
        "create a new one.");
  }
  CF_EXPECT(std::move(flock_res));
  return fd;
}

bool InstanceManager::IsGroupTransitionInProgress(
    const LocalInstanceGroup& group) {
  std::string lock_path =
      fmt::format("{}/{}", group.BaseDir(), kGroupInProgressLockName);
  Result<Fd, int> fd = Fd::Open(lock_path, O_RDONLY | O_CLOEXEC);
  if (!fd.has_value()) {
    return false;
  }
  Result<void, int> flock_res = fd->Flock(LOCK_SH | LOCK_NB);
  return !flock_res.has_value() && *flock_res.error() == EWOULDBLOCK;
}

Result<std::pair<LocalInstance, LocalInstanceGroup>>
InstanceManager::FindInstanceWithGroup(
    const InstanceDatabase::Filter& filter) const {
  return CF_EXPECT(instance_db_.FindInstanceWithGroup(filter));
}

Result<std::vector<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>>
InstanceManager::FindInstances(const InstanceDatabase::Filter& filter) const {
  return CF_EXPECT(instance_db_.FindInstances(filter));
}

Result<bool> InstanceManager::HasInstanceGroups() const {
  return !CF_EXPECT(instance_db_.IsEmpty());
}

Result<std::vector<InstanceManager::InternalInstanceDesc>>
InstanceManager::AllocateAndLockInstanceIds(
    std::vector<InstanceParams> instances) {
  std::set<unsigned> requested;
  for (const auto& instance : instances) {
    if (instance.instance_id.has_value()) {
      unsigned id = instance.instance_id.value();
      auto [_, inserted] = requested.insert(id);
      CF_EXPECTF(std::move(inserted),
                 "Requested instance ids must be distinct, but {} is repeated",
                 id);
    }
  }
  if (lock_manager_ == nullptr) {
    std::set<unsigned> used_ids = requested;
    for (const LocalInstanceGroup& group :
         CF_EXPECT(instance_db_.FindGroups({}))) {
      for (const LocalInstance& instance : group.Instances()) {
        used_ids.insert(instance.Id());
      }
    }
    std::vector<InternalInstanceDesc> ret;
    ret.reserve(instances.size());
    unsigned next_id = 1;
    for (auto& instance : instances) {
      unsigned id;
      if (instance.instance_id.has_value()) {
        id = *instance.instance_id;
      } else {
        while (used_ids.count(next_id) > 0) {
          ++next_id;
        }
        id = next_id++;
        used_ids.insert(id);
      }
      ret.emplace_back(InternalInstanceDesc{
          .id = id,
          .lock_file = std::nullopt,
          .name = std::move(instance.per_instance_name),
      });
    }
    return ret;
  }

  // Acquire requested locks in ascending ID order to avoid ABBA deadlocks.
  std::unordered_map<unsigned, InstanceLockFile> requested_lock_files;
  requested_lock_files.reserve(requested.size());
  for (unsigned id : requested) {
    InstanceLockFile lock = CF_EXPECT(lock_manager_->AcquireLock(id));
    CF_EXPECTF(CF_EXPECT(lock.Status()) == InUseState::kNotInUse,
               "Instance ID {} is already in use", id);
    requested_lock_files.emplace(id, std::move(lock));
  }
  std::vector<InternalInstanceDesc> ret;
  ret.reserve(instances.size());
  for (auto& instance : instances) {
    if (instance.instance_id.has_value()) {
      auto lock_it = requested_lock_files.find(*instance.instance_id);
      CF_EXPECT(lock_it != requested_lock_files.end());
      unsigned id = lock_it->second.Instance();
      ret.emplace_back(InternalInstanceDesc{
          .id = id,
          .lock_file = std::move(lock_it->second),
          .name = std::move(instance.per_instance_name),
      });
    } else {
      InstanceLockFile lock = CF_EXPECT(lock_manager_->AcquireUnusedLock());
      unsigned id = lock.Instance();
      ret.emplace_back(InternalInstanceDesc{
          .id = id,
          .lock_file = std::move(lock),
          .name = std::move(instance.per_instance_name),
      });
    }
  }
  return ret;
}

Result<LocalInstanceGroup> InstanceManager::CreateInstanceGroup(
    InstanceGroupParams group_params, GroupDirectories directories,
    std::optional<Fd>* out_in_progress_lock) {
  CF_EXPECT_EQ(
      group_params.instances.size(), directories.product_out_paths.size(),
      "Number of product directories doesn't match number of instances");
  CF_EXPECT(PruneBrokenInstanceGroups());

  std::vector<InternalInstanceDesc> instance_descs =
      CF_EXPECT(AllocateAndLockInstanceIds(std::move(group_params.instances)));

  CF_EXPECT(EnsureDirectoryExists(base_root_dir_, 0750));
  constexpr int kMaxBaseDirAttempts = 1024;
  std::optional<LocalInstanceGroup> built_group;
  for (int attempt = 0; attempt < kMaxBaseDirAttempts; ++attempt) {
    LocalInstanceGroup::Builder group_builder(group_params.group_name,
                                              base_root_dir_);
    for (const auto& instance_desc : instance_descs) {
      if (instance_desc.name.has_value()) {
        group_builder.AddInstance(instance_desc.id, instance_desc.name.value());
      } else {
        group_builder.AddInstance(instance_desc.id);
      }
    }
    LocalInstanceGroup candidate = CF_EXPECT(group_builder.Build());

    ClaimBaseDirResult claim_res = CF_EXPECT(
        TryClaimGroupBaseDir(directories.base_directory, candidate.BaseDir()));
    if (claim_res == ClaimBaseDirResult::kClaimed) {
      built_group.emplace(std::move(candidate));
      break;
    }
  }
  CF_EXPECT(built_group.has_value(),
            "Failed to allocate unique group base directory");
  LocalInstanceGroup group = std::move(*built_group);

  auto cleanup_group_dir = [this, &group]() {
    (void)RemoveGroupDirectory(group, base_root_dir_);
  };

  // The base and other directories are always set to the default location, if
  // the user provides custom directories the ones in the default locations
  // become symbolic links to those.
  Result<void> dirs_res =
      PopulateGroupSubdirectories(group, std::move(directories));
  if (!dirs_res.has_value()) {
    cleanup_group_dir();
  }
  CF_EXPECT(std::move(dirs_res));

  std::optional<Fd> in_progress_lock;
  if (out_in_progress_lock != nullptr) {
    Result<Fd> lock_res = AcquireGroupInProgressLock(group);
    if (!lock_res.has_value()) {
      cleanup_group_dir();
    }
    in_progress_lock = CF_EXPECT(std::move(lock_res));
  }

  Result<void> add_res = instance_db_.AddInstanceGroup(group);
  if (!add_res.has_value()) {
    in_progress_lock.reset();
    cleanup_group_dir();
  }
  CF_EXPECT(std::move(add_res));

  for (size_t idx = 0; idx < instance_descs.size(); ++idx) {
    if (!instance_descs[idx].lock_file.has_value()) {
      continue;
    }
    Result<void> status_res =
        instance_descs[idx].lock_file->Status(InUseState::kInUse);
    if (!status_res.has_value()) {
      for (size_t prev = 0; prev < idx; ++prev) {
        if (instance_descs[prev].lock_file.has_value()) {
          (void)instance_descs[prev].lock_file->Status(InUseState::kNotInUse);
        }
      }
      (void)instance_db_.RemoveInstanceGroup(group.GroupName());
      in_progress_lock.reset();
      cleanup_group_dir();
    }
    CF_EXPECT(std::move(status_res));
  }
  if (out_in_progress_lock != nullptr) {
    *out_in_progress_lock = std::move(in_progress_lock);
  }

  GatherVmInstantiationMetrics(group);

  return group;
}

Result<void> InstanceManager::EnsureInstanceLocksInUse(
    const LocalInstanceGroup& group) {
  if (lock_manager_ == nullptr) {
    return {};
  }
  std::set<unsigned> sorted_ids;
  for (const LocalInstance& instance : group.Instances()) {
    if (instance.Id() != 0) {
      sorted_ids.insert(instance.Id());
    }
  }
  std::vector<InstanceLockFile> locks;
  locks.reserve(sorted_ids.size());
  for (unsigned id : sorted_ids) {
    InstanceLockFile lock = CF_EXPECT(lock_manager_->AcquireLock(id));
    if (CF_EXPECT(lock.Status()) == InUseState::kInUse) {
      std::optional<uid_t> holder = CF_EXPECT(lock.HolderUid());
      if (holder.has_value()) {
        CF_EXPECTF(*holder == geteuid(),
                   "Instance ID {} is already in use by UID {}", id, *holder);
      }
    }
    locks.push_back(std::move(lock));
  }
  for (InstanceLockFile& lock : locks) {
    CF_EXPECT(lock.Status(InUseState::kInUse));
  }
  return {};
}

Result<bool> InstanceManager::RemoveInstanceGroup(LocalInstanceGroup group) {
  CF_EXPECT(!group.HasActiveInstances(),
            "Group still contains active instances");
  CF_EXPECT(!IsGroupTransitionInProgress(group),
            "Group transition is currently in progress");

  bool removed = CF_EXPECT(instance_db_.RemoveInstanceGroup(
      group, [&group]() { return !IsGroupTransitionInProgress(group); }));
  if (!removed) {
    return false;
  }

  if (lock_manager_ != nullptr) {
    std::set<unsigned> sorted_ids;
    for (const LocalInstance& instance : group.Instances()) {
      if (instance.Id() != 0) {
        sorted_ids.insert(instance.Id());
      }
    }
    for (unsigned id : sorted_ids) {
      if (Result<void> res = lock_manager_->ResetLockFile(id);
          !res.has_value()) {
        LOG(ERROR) << "Failed to reset instance id lock " << id << ": "
                   << res.error();
      }
    }
  }
  CF_EXPECT(RemoveGroupDirectory(group, base_root_dir_));
  return true;
}

Result<LocalInstanceGroup> InstanceManager::RefreshInstanceGroupState(
    LocalInstanceGroup group, bool caller_holds_in_progress_lock) {
  std::optional<std::vector<ActiveRunCvdProc>> cached_procs;
  CF_EXPECT(RefreshGroupWithProcs(group, cached_procs, instance_db_,
                                  caller_holds_in_progress_lock));
  return group;
}

Result<void> InstanceManager::RefreshInstanceGroups() {
  std::vector<LocalInstanceGroup> groups =
      CF_EXPECT(instance_db_.FindGroups({}));
  std::optional<std::vector<ActiveRunCvdProc>> cached_procs;
  std::vector<InstanceDatabase::GroupStateUpdate> batch_updates;
  for (const LocalInstanceGroup& group : groups) {
    std::unordered_map<unsigned, cvd::InstanceState> updates = CF_EXPECT(
        ProbeGroupStateUpdates(group, cached_procs,
                               /*caller_holds_in_progress_lock=*/false));
    if (!updates.empty()) {
      batch_updates.push_back(InstanceDatabase::GroupStateUpdate{
          .snapshot = group,
          .updated_states = std::move(updates),
          .precondition =
              [group]() {
                return !InstanceManager::IsGroupTransitionInProgress(group);
              },
      });
    }
  }
  if (!batch_updates.empty()) {
    (void)CF_EXPECT(
        instance_db_.BatchCompareAndUpdateInstanceStates(batch_updates));
  }
  return {};
}

Result<void> InstanceManager::PruneBrokenInstanceGroups() {
  CF_EXPECT(RefreshInstanceGroups());
  std::vector<LocalInstanceGroup> groups =
      CF_EXPECT(instance_db_.FindGroups({}));
  for (LocalInstanceGroup& group : groups) {
    if (group.HasActiveInstances()) {
      continue;
    }
    if (!IsGroupBackingDirsIntact(group)) {
      LOG(INFO) << "Pruning inactive instance group '" << group.GroupName()
                << "' with missing backing directories";
      Result<bool> remove_res = RemoveInstanceGroup(std::move(group));
      if (!remove_res.has_value()) {
        LOG(WARNING) << "Failed to prune broken instance group: "
                     << remove_res.error();
      }
    }
  }
  return {};
}

Result<void> InstanceManager::UpdateInstanceGroup(
    const LocalInstanceGroup& group) {
  CF_EXPECT(instance_db_.UpdateInstanceGroup(group));
  return {};
}

Result<void> InstanceManager::StopInstanceGroup(
    LocalInstanceGroup& group, InstanceDirActionOnStop instance_dir_action,
    std::chrono::seconds launcher_timeout,
    const std::vector<unsigned>& instance_nums) {
  // Validate that the requested instances actually belong to this group
  std::set<unsigned> valid_ids;
  for (const auto& instance : group.Instances()) {
    valid_ids.insert(instance.Id());
  }
  std::set<unsigned> ids_to_stop;
  for (unsigned num : instance_nums) {
    CF_EXPECTF(valid_ids.count(num) != 0,
               "Instance ID {} does not belong to group '{}'", num,
               group.GroupName());
    ids_to_stop.insert(num);
  }

  const auto stop_bin = CF_EXPECT(StopBin(group.HostArtifactsPath()));
  const auto stop_bin_path = group.HostArtifactsPath() + "/bin/" + stop_bin;
  Result<void> cmd_result = RunStopCvd(StopCvdParams{
      .bin_path = stop_bin_path,
      .home_dir = group.HomeDir(),
      .wait_for_launcher_secs = static_cast<int>(launcher_timeout.count()),
      .clear_runtime_dirs =
          instance_dir_action == InstanceDirActionOnStop::Clear,
      .instance_nums = instance_nums,
  });
  if (!cmd_result.has_value()) {
    LOG(WARNING)
        << "Warning: error stopping instances for dir \"" + group.HomeDir() +
               "\".\nThis can happen if instances are already stopped.\n";
    return cmd_result;
  }
  if (instance_nums.empty()) {
    group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  } else {
    for (auto& instance : group.Instances()) {
      if (ids_to_stop.count(instance.Id()) > 0) {
        instance.SetState(cvd::INSTANCE_STATE_STOPPED);
      }
    }
  }
  // TODO: b/471069557 - diagnose unused
  Result<void> unused = instance_db_.UpdateInstanceGroup(group);
  return {};
}

Result<void> InstanceManager::Clear() {
  if (Result<void> refresh_res = RefreshInstanceGroups();
      !refresh_res.has_value()) {
    LOG(WARNING) << "Failed to refresh instance groups before clear: "
                 << refresh_res.error();
  }
  const std::string config_json_name =
      android::base::Basename(GetGlobalConfigFileLink());
  auto instance_groups =
      CF_EXPECT(instance_db_.Clear(), "Failed to clear instance database");
  for (auto& group : instance_groups) {
    // Only stop running instances.
    if (group.HasActiveInstances()) {
      Result<void> stop_result =
          StopInstanceGroup(group, InstanceDirActionOnStop::Clear);
      if (!stop_result.has_value()) {
        LOG(ERROR) << stop_result.error();
      }
    }
    if (lock_manager_ != nullptr) {
      for (auto instance : group.Instances()) {
        if (instance.Id() <= 0) {
          continue;
        }
        auto res = lock_manager_->ResetLockFile(instance.Id());
        if (!res.has_value()) {
          LOG(ERROR) << "Failed to reset lock file for instance: "
                     << res.error();
        }
      }
    }
    std::string runtime_link = group.HomeDir() + "/cuttlefish_runtime";
    if (Result<void, int> res = RemoveFile(runtime_link); !res.has_value()) {
      LOG(ERROR) << res.error();
    }
    std::string config_link = group.HomeDir() + "/" + config_json_name;
    if (Result<void, int> res = RemoveFile(config_link); !res.has_value()) {
      LOG(ERROR) << res.error();
    }
    // TODO: b/471069557 - diagnose unused
    Result<void> unused = RemoveGroupDirectory(group, base_root_dir_);
  }
  std::cerr << "Stopped all known instances\n";
  return {};
}

Result<void> InstanceManager::ForcefullyStopGroup(
    const unsigned any_id_in_group) {
  CF_EXPECT(cuttlefish::ForcefullyStopGroup(any_id_in_group, lock_manager_));
  return {};
}

Result<void> InstanceManager::Reset() {
  CF_EXPECT(Clear());
  CF_EXPECT(KillAllCuttlefishInstances(false, lock_manager_));
  return {};
}

Result<void> InstanceManager::ResetAndClearInstanceDirs() {
  CF_EXPECT(Clear());
  CF_EXPECT(KillAllCuttlefishInstances(true, lock_manager_));
  return {};
}

Result<std::vector<LocalInstanceGroup>> InstanceManager::FindGroups(
    const InstanceDatabase::Filter& filter) const {
  return CF_EXPECT(instance_db_.FindGroups(filter));
}

Result<LocalInstanceGroup> InstanceManager::FindGroup(
    const InstanceDatabase::Filter& filter) const {
  auto output = CF_EXPECT(instance_db_.FindGroups(filter));
  CF_EXPECT_EQ(output.size(), 1ul);
  return *(output.begin());
}

}  // namespace cuttlefish
