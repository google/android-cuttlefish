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
#include "host/commands/assemble_cvd/clean.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include <regex>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/flags.h"

namespace cuttlefish {
namespace {

Result<void> CleanPriorFiles(const std::string& path,
                             const std::set<std::string>& preserving) {
  if (preserving.count(cpp_basename(path))) {
    LOG(DEBUG) << "Preserving: " << path;
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
    LOG(DEBUG) << "Deleting: " << path;
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
    CF_EXPECT(CleanPriorFiles(entity_path.c_str(), preserving),
              "CleanPriorFiles for \""
                  << path << "\" failed on recursing into \"" << entity_path
                  << "\"");
  }
  if (rmdir(path.c_str()) < 0) {
    if (!(errno == EEXIST || errno == ENOTEMPTY)) {
      // If EEXIST or ENOTEMPTY, probably because a file was preserved
      return CF_ERRNO("Could not rmdir \"" << path << "\"");
    }
  }
  return {};
}

Result<void> CleanPriorFiles(const std::vector<std::string>& paths,
                             const std::set<std::string>& preserving) {
  std::string prior_files;
  for (auto path : paths) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) < 0 && errno != ENOENT) {
      // If ENOENT, it doesn't exist yet, so there is no work to do'
      return CF_ERRNO("Could not stat \"" << path << "\"");
    }
    bool is_directory = (statbuf.st_mode & S_IFMT) == S_IFDIR;
    prior_files += (is_directory ? (path + "/*") : path) + " ";
  }
  LOG(DEBUG) << "Assuming prior files of " << prior_files;
  std::string lsof_cmd = "lsof -t " + prior_files + " >/dev/null 2>&1";
  int rval = std::system(lsof_cmd.c_str());
  // lsof returns 0 if any of the files are open
  CF_EXPECT(WEXITSTATUS(rval) != 0, "Clean aborted: files are in use");
  for (const auto& path : paths) {
    CF_EXPECT(CleanPriorFiles(path, preserving),
              "CleanPriorFiles failed for \"" << path << "\"");
  }
  return {};
}

} // namespace

Result<void> CleanPriorFiles(const std::set<std::string>& preserving,
                             const std::string& assembly_dir,
                             const std::vector<std::string>& instance_dirs) {
  std::vector<std::string> paths = {
    // Everything in the assembly directory
    assembly_dir,
    // The environment file
    GetCuttlefishEnvPath(),
    // The global link to the config file
    GetGlobalConfigFileLink(),
  };
  paths.insert(paths.end(), instance_dirs.begin(), instance_dirs.end());
  using android::base::Join;
  CF_EXPECT(CleanPriorFiles(paths, preserving),
            "CleanPriorFiles("
                << "paths = {" << Join(paths, ", ") << "}, "
                << "preserving = {" << Join(preserving, ", ") << "}) failed");
  return {};
}

} // namespace cuttlefish
