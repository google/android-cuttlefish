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

#include "host/commands/cvd/selector/selector_constants.h"

#include <sys/stat.h>
#include <unistd.h>

#include <deque>
#include <sstream>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/users.h"

namespace cuttlefish {
namespace selector {

enum class OwnershipType { kUser, kGroup, kOthers };

static OwnershipType GetOwnershipType(const struct stat& file_stat,
                                      const uid_t uid, const gid_t gid) {
  if (file_stat.st_uid == uid) {
    return OwnershipType::kUser;
  }
  if (file_stat.st_gid == gid) {
    return OwnershipType::kGroup;
  }
  return OwnershipType::kOthers;
}

struct RequirePermission {
  const bool needs_read_permission;
  const bool needs_write_permission;
  const bool needs_exec_permission;
};

static Result<void> CheckPermission(const OwnershipType ownership_type,
                                    const struct stat& file_stat,
                                    const RequirePermission& perm) {
  const auto perm_bits = file_stat.st_mode;

  switch (ownership_type) {
    case OwnershipType::kUser: {
      CF_EXPECT(!perm.needs_read_permission || (perm_bits & S_IRUSR));
      CF_EXPECT(!perm.needs_write_permission || (perm_bits & S_IWUSR));
      CF_EXPECT(!perm.needs_exec_permission || (perm_bits & S_IXUSR));
      return {};
    }
    case OwnershipType::kGroup: {
      CF_EXPECT(!perm.needs_read_permission || (perm_bits & S_IRGRP));
      CF_EXPECT(!perm.needs_write_permission || (perm_bits & S_IWGRP));
      CF_EXPECT(!perm.needs_exec_permission || (perm_bits & S_IXGRP));
      return {};
    }
    case OwnershipType::kOthers:
      break;
  }
  CF_EXPECT(!perm.needs_read_permission || (perm_bits & S_IROTH));
  CF_EXPECT(!perm.needs_write_permission || (perm_bits & S_IWOTH));
  CF_EXPECT(!perm.needs_exec_permission || (perm_bits & S_IXOTH));
  return {};
}

static Result<void> CheckPermission(const std::string& dir,
                                    const uid_t client_uid,
                                    const gid_t client_gid) {
  CF_EXPECT(!dir.empty() && DirectoryExists(dir));
  struct stat dir_stat;
  CF_EXPECT_EQ(stat(dir.c_str(), std::addressof(dir_stat)), 0);

  const auto server_ownership = GetOwnershipType(dir_stat, getuid(), getgid());
  CF_EXPECT(CheckPermission(server_ownership, dir_stat,
                            RequirePermission{.needs_read_permission = true,
                                              .needs_write_permission = true,
                                              .needs_exec_permission = true}));
  const auto client_ownership =
      GetOwnershipType(dir_stat, client_uid, client_gid);
  CF_EXPECT(CheckPermission(client_ownership, dir_stat,
                            RequirePermission{.needs_read_permission = true,
                                              .needs_write_permission = true,
                                              .needs_exec_permission = true}));
  return {};
}

Result<std::string> ParentOfAutogeneratedHomes(const uid_t client_uid,
                                               const gid_t client_gid) {
  std::deque<std::string> try_dirs = {
      StringFromEnv("TMPDIR", ""),
      StringFromEnv("TEMP", ""),
      StringFromEnv("TMP", ""),
      "/tmp",
      "/var/tmp",
      "/usr/tmp",
  };

  auto system_wide_home = SystemWideUserHome(client_uid);
  if (system_wide_home.ok()) {
    try_dirs.emplace_back(*system_wide_home);
  }
  try_dirs.emplace_back(AbsolutePath("."));
  while (!try_dirs.empty()) {
    const auto candidate = std::move(try_dirs.front());
    try_dirs.pop_front();
    if (candidate.empty() || !EnsureDirectoryExists(candidate).ok()) {
      continue;
    }
    CF_EXPECT(CheckPermission(candidate, client_uid, client_gid));
    return AbsolutePath(candidate);
  }
  return CF_ERR("Tried all candidate directories but none was read-writable.");
}

SelectorFlag<std::string> SelectorFlags::GroupNameFlag(
    const std::string& name) {
  SelectorFlag<std::string> group_name{name};
  std::stringstream group_name_help;
  group_name_help << "--" << name << "=<"
                  << "name of the instance group>";
  group_name.SetHelpMessage(group_name_help.str());
  return group_name;
}

SelectorFlag<std::string> SelectorFlags::InstanceNameFlag(
    const std::string& name) {
  SelectorFlag<std::string> instance_name{name};
  std::stringstream instance_name_help;
  instance_name_help << "--" << name << "=<"
                     << "comma-separated names of the instances>";
  instance_name.SetHelpMessage(instance_name_help.str());
  return instance_name;
}

SelectorFlag<bool> SelectorFlags::DisableDefaultGroupFlag(
    const std::string& name, const bool default_val) {
  SelectorFlag<bool> disable_default_group(name, default_val);
  std::stringstream help;
  help << "--" << name << "=true not to create the default instance group.";
  disable_default_group.SetHelpMessage(help.str());
  return disable_default_group;
}

SelectorFlag<bool> SelectorFlags::AcquireFileLockFlag(const std::string& name,
                                                      const bool default_val) {
  SelectorFlag<bool> acquire_file_lock(name, default_val);
  std::stringstream help;
  help << "--" << name
       << "=false for cvd server not to acquire lock file locks.";
  acquire_file_lock.SetHelpMessage(help.str());
  return acquire_file_lock;
}

const SelectorFlags& SelectorFlags::Get() {
  static SelectorFlags singleton_selector_flags;
  return singleton_selector_flags;
}

}  // namespace selector
}  // namespace cuttlefish
