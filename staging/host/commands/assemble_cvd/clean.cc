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

#include "host/commands/assemble_cvd/flags.h"
#include "common/libs/utils/files.h"

namespace cuttlefish {
namespace {

bool CleanPriorFiles(const std::string& path, const std::set<std::string>& preserving) {
  if (preserving.count(cpp_basename(path))) {
    LOG(DEBUG) << "Preserving: " << path;
    return true;
  }
  struct stat statbuf;
  if (lstat(path.c_str(), &statbuf) < 0) {
    int error_num = errno;
    if (error_num == ENOENT) {
      return true;
    } else {
      LOG(ERROR) << "Could not stat \"" << path << "\": " << strerror(error_num);
      return false;
    }
  }
  if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
    LOG(DEBUG) << "Deleting: " << path;
    if (unlink(path.c_str()) < 0) {
      int error_num = errno;
      LOG(ERROR) << "Could not unlink \"" << path << "\", error was " << strerror(error_num);
      return false;
    }
    return true;
  }
  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(path.c_str()), closedir);
  if (!dir) {
    int error_num = errno;
    LOG(ERROR) << "Could not clean \"" << path << "\": error was " << strerror(error_num);
    return false;
  }
  for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
    std::string entity_name(entity->d_name);
    if (entity_name == "." || entity_name == "..") {
      continue;
    }
    std::string entity_path = path + "/" + entity_name;
    if (!CleanPriorFiles(entity_path.c_str(), preserving)) {
      return false;
    }
  }
  if (rmdir(path.c_str()) < 0) {
    if (!(errno == EEXIST || errno == ENOTEMPTY)) {
      // If EEXIST or ENOTEMPTY, probably because a file was preserved
      int error_num = errno;
      LOG(ERROR) << "Could not rmdir \"" << path << "\", error was " << strerror(error_num);
      return false;
    }
  }
  return true;
}

bool CleanPriorFiles(const std::vector<std::string>& paths, const std::set<std::string>& preserving) {
  std::string prior_files;
  for (auto path : paths) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) < 0 && errno != ENOENT) {
      // If ENOENT, it doesn't exist yet, so there is no work to do'
      int error_num = errno;
      LOG(ERROR) << "Could not stat \"" << path << "\": " << strerror(error_num);
      return false;
    }
    bool is_directory = (statbuf.st_mode & S_IFMT) == S_IFDIR;
    prior_files += (is_directory ? (path + "/*") : path) + " ";
  }
  LOG(DEBUG) << "Assuming prior files of " << prior_files;
  std::string lsof_cmd = "lsof -t " + prior_files + " >/dev/null 2>&1";
  int rval = std::system(lsof_cmd.c_str());
  // lsof returns 0 if any of the files are open
  if (WEXITSTATUS(rval) == 0) {
    LOG(ERROR) << "Clean aborted: files are in use";
    return false;
  }
  for (const auto& path : paths) {
    if (!CleanPriorFiles(path, preserving)) {
      LOG(ERROR) << "Remove of file under \"" << path << "\" failed";
      return false;
    }
  }
  return true;
}

} // namespace

bool CleanPriorFiles(
    const std::set<std::string>& preserving,
    const std::string& assembly_dir,
    const std::string& instance_dir) {
  std::vector<std::string> paths = {
    // Everything in the assembly directory
    assembly_dir,
    // The environment file
    GetCuttlefishEnvPath(),
    // The global link to the config file
    GetGlobalConfigFileLink(),
  };

  std::string runtime_dir_parent = cpp_dirname(AbsolutePath(instance_dir));
  std::string runtime_dirs_basename = cpp_basename(AbsolutePath(instance_dir));

  std::regex instance_dir_regex("^.+\\.[1-9]\\d*$");
  for (const auto& path : DirectoryContents(runtime_dir_parent)) {
    std::string absl_path = runtime_dir_parent + "/" + path;
    if((path.rfind(runtime_dirs_basename, 0) == 0) && std::regex_match(path, instance_dir_regex) &&
        DirectoryExists(absl_path)) {
      paths.push_back(absl_path);
    }
  }
  paths.push_back(instance_dir);
  return CleanPriorFiles(paths, preserving);
}

bool EnsureDirectoryExists(const std::string& directory_path) {
  if (!DirectoryExists(directory_path)) {
    LOG(DEBUG) << "Setting up " << directory_path;
    if (mkdir(directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
        && errno != EEXIST) {
      PLOG(ERROR) << "Failed to create dir: \"" << directory_path << "\" ";
      return false;
    }
  }
  return true;
}

} // namespace cuttlefish
