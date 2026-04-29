/*
 * Copyright (C) 2020 The Android Open Source Project
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
#include "cuttlefish/host/commands/assemble_cvd/clean.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include <vector>

#include <android-base/file.h>
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include <fmt/ranges.h>  // NOLINT(misc-include-cleaner): version difference
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/proc_file_utils.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/tracing/tracing.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<void> CleanPriorFiles(const std::string& path,
                             const std::set<std::string>& preserving) {
  if (preserving.count(android::base::Basename(path))) {
    VLOG(0) << "Preserving: " << path;
    return {};
  }
  struct stat statbuf;
  if (lstat(path.c_str(), &statbuf) < 0) {
    int error_num = errno;
    if (error_num == ENOENT) {
      return {};
    } else {
      return CF_ERRNO("Could not stat \"" << path);
    }
  }
  if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
    VLOG(0) << "Deleting: " << path;
    if (unlink(path.c_str()) < 0) {
      return CF_ERRNO("Could not unlink \"" << path << "\"");
    }
    return {};
  }
  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(path.c_str()), closedir);
  if (!dir) {
    return CF_ERRNO("Could not clean \"" << path << "\"");
  }
  for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
    std::string entity_name(entity->d_name);
    if (entity_name == "." || entity_name == "..") {
      continue;
    }
    std::string entity_path = path + "/" + entity_name;
    CF_EXPECT(CleanPriorFiles(entity_path, preserving),
              "CleanPriorFiles for \""
                  << path << "\" failed on recursing into \"" << entity_path
                  << "\"");
  }
  if (rmdir(path.c_str()) < 0) {
    if (!(errno == EEXIST || errno == ENOTEMPTY || errno == EROFS ||
          errno == EBUSY)) {
      // If EEXIST or ENOTEMPTY, probably because a file was preserved. EROFS
      // or EBUSY likely means a bind mount for host-sandboxing mode.
      return CF_ERRF("Could not rmdir '{}': '{}'", path, StrError(errno));
    }
  }
  return {};
}

Result<std::vector<pid_t>> GetPidsUsingFiles(
    const std::set<std::string>& prior_dirs,
    const std::set<std::string>& prior_files) {
  std::vector<pid_t> pids_in_use;

  VLOG(0) << "Checking if prior dirs or files are in use: ";
  for (const auto& prior_dir : prior_dirs) {
    VLOG(0) << prior_dir;
  }
  for (const auto& prior_file : prior_files) {
    VLOG(0) << prior_file;
  }
  auto pids = CF_EXPECT(CollectPids(getuid()));
  for (const auto pid : pids) {
    std::string fd_dir_path = fmt::format("/proc/{}/fd", pid);
    Result<std::vector<std::string>> entity_names =
        DirectoryContents(fd_dir_path);

    if (!entity_names.ok()) {
      continue;
    }

    for (const auto& entity_name : *entity_names) {
      std::string fd_path = fd_dir_path + "/" + entity_name;
      std::string target;
      if (!android::base::Readlink(fd_path, &target)) {
        continue;
      }

      bool match = false;

      for (const auto& prior_dir : prior_dirs) {
        if (target.starts_with(prior_dir)) {
          match = true;
          break;
        }
      }

      if (!match && prior_files.find(target) != prior_files.end()) {
        match = true;
      }

      if (match) {
        pids_in_use.push_back(pid);
        break;
      }
    }
  }

  return pids_in_use;
}

Result<void> CleanPriorFiles(const std::vector<std::string>& paths,
                             const std::set<std::string>& preserving) {
  std::set<std::string> prior_dirs;
  std::set<std::string> prior_files;
  for (const auto& path : paths) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) < 0) {
      if (errno == ENOENT) {
        continue;  // it doesn't exist yet, so there is no work to do
      }
      return CF_ERRNO("Could not stat \"" << path << "\"");
    }
    bool is_directory = (statbuf.st_mode & S_IFMT) == S_IFDIR;
    (is_directory ? prior_dirs : prior_files).emplace(path);
  }
  VLOG(0) << fmt::format("Prior dirs: {}", fmt::join(prior_dirs, ", "));
  VLOG(0) << fmt::format("Prior files: {}", fmt::join(prior_files, ", "));

  // TODO(schuffelen): Fix logic for host-sandboxing mode.
  if (!InSandbox() && (!prior_dirs.empty() || !prior_files.empty())) {
    auto pids = CF_EXPECT(GetPidsUsingFiles(prior_dirs, prior_files));
    CF_EXPECTF(
        pids.empty(),
        "Instance directory files in use. Try `cvd reset`? Observed PIDs: {}",
        fmt::join(pids, ", "));
  }

  for (const auto& path : paths) {
    CF_EXPECT(CleanPriorFiles(path, preserving),
              "CleanPriorFiles failed for \"" << path << "\"");
  }
  return {};
}

} // namespace

Result<void> CleanPriorFiles(const std::set<std::string>& preserving,
                             const std::vector<std::string>& clean_dirs) {
  CF_TRACE("CleanPriorFiles");

  std::vector<std::string> paths = {
      // The global link to the config file
      GetGlobalConfigFileLink(),
  };
  paths.insert(paths.end(), clean_dirs.begin(), clean_dirs.end());
  using absl::StrJoin;
  CF_EXPECT(CleanPriorFiles(paths, preserving),
            "CleanPriorFiles("
                << "paths = {" << StrJoin(paths, ", ") << "}, "
                << "preserving = {" << StrJoin(preserving, ", ") << "}) failed");
  return {};
}

} // namespace cuttlefish
