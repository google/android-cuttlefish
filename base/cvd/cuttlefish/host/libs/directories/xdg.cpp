//
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/libs/directories/xdg.h"

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"

// See https://specifications.freedesktop.org/basedir-spec/latest/

namespace cuttlefish {
namespace {

std::optional<std::string> NonEmptyEnv(const std::string& var_name) {
  std::optional<std::string> opt = StringFromEnv(var_name);
  return opt.has_value() && !opt->empty() ? opt : std::optional<std::string>();
}

Result<std::string> XdgDataHome() {
  std::string home = CF_EXPECT(SystemWideUserHome());
  return NonEmptyEnv("XDG_DATA_HOME").value_or(home + "/.local/share");
}

Result<std::string> XdgConfigHome() {
  std::string home = CF_EXPECT(SystemWideUserHome());
  return NonEmptyEnv("XDG_CONFIG_HOME").value_or(home + "/.config");
}

Result<std::string> XdgStateHome() {
  std::string home = CF_EXPECT(SystemWideUserHome());
  return NonEmptyEnv("XDG_STATE_HOME").value_or(home + "/.local/state");
}

Result<std::string> XdgCacheHome() {
  std::string home = CF_EXPECT(SystemWideUserHome());
  return NonEmptyEnv("XDG_CACHE_HOME").value_or(home + ".cache");
}

std::string XdgRuntimeDir() {
  return NonEmptyEnv("XDG_RUNTIME_DIR").value_or("/tmp");
}

Result<std::vector<std::string>> XdgDataDirs() {
  static constexpr char kDefault[] = "/usr/local/share/:/usr/share/";
  std::string str = NonEmptyEnv("XDG_DATA_DIRS").value_or(kDefault);
  std::vector<std::string> dirs = android::base::Tokenize(str, ":");
  dirs.emplace(dirs.begin(), CF_EXPECT(XdgDataHome()));
  return dirs;
}

Result<std::vector<std::string>> XdgConfigDirs() {
  std::string str = NonEmptyEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
  std::vector<std::string> dirs = android::base::Tokenize(str, ":");
  dirs.emplace(dirs.begin(), CF_EXPECT(XdgConfigHome()));
  return dirs;
}

constexpr char kCvdSuffix[] = "/cvd";

}  // namespace

Result<std::string> CvdDataHome() {
  return CF_EXPECT(XdgDataHome()) + kCvdSuffix;
}

Result<std::string> CvdConfigHome() {
  return CF_EXPECT(XdgConfigHome()) + kCvdSuffix;
}

Result<std::string> CvdStateHome() {
  return CF_EXPECT(XdgStateHome()) + kCvdSuffix;
}

Result<std::string> CvdCacheHome() {
  return CF_EXPECT(XdgCacheHome()) + kCvdSuffix;
}

std::string CvdRuntimeDir() { return XdgRuntimeDir() + kCvdSuffix; }

Result<std::vector<std::string>> CvdDataDirs() {
  std::vector<std::string> dirs = CF_EXPECT(XdgDataDirs());
  for (std::string& dir : dirs) {
    dir += kCvdSuffix;
  }
  return dirs;
}

Result<std::vector<std::string>> CvdConfigDirs() {
  std::vector<std::string> dirs = CF_EXPECT(XdgConfigDirs());
  for (std::string& dir : dirs) {
    dir += kCvdSuffix;
  }
  return dirs;
}

Result<std::string> ReadCvdDataFile(std::string_view path) {
  for (const auto& dir : CF_EXPECT(CvdDataDirs())) {
    std::string contents;
    if (android::base::ReadFileToString(fmt::format("{}/{}", dir, path),
                                        &contents)) {
      return contents;
    }
  }
  return CF_ERRF("Not able to open '{}'", path);
}

Result<std::vector<std::string>> FindCvdDataFiles(std::string_view path) {
  std::vector<std::string> results;
  for (const std::string& dir : CF_EXPECT(CvdDataDirs())) {
    struct stat statbuf;
    std::string test_path = fmt::format("{}/{}", dir, path);
    if (stat(test_path.c_str(), &statbuf) != 0) {
      continue;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
      results.emplace_back(std::move(test_path));
      continue;
    }
    std::unique_ptr<DIR, int (*)(DIR*)> dir_iter(opendir(test_path.c_str()),
                                                 closedir);
    CF_EXPECTF(dir_iter.get(), "Failed to open '{}'", path);
    dirent* entry;
    while ((entry = readdir(dir_iter.get())) != nullptr) {
      std::string entry_name(entry->d_name);
      if (entry_name == "." || entry_name == "..") {
        continue;
      }
      results.emplace_back(fmt::format("{}/{}", test_path, entry->d_name));
    }
  }
  return results;
}

Result<void> WriteCvdDataFile(std::string_view path, std::string contents) {
  std::string full_path = fmt::format("{}/{}", CF_EXPECT(CvdDataHome()), path);
  CF_EXPECT(EnsureDirectoryExists(android::base::Dirname(full_path), 0700));

  std::string full_path_template = full_path + ".temp.XXXXXX";
  int file_raw_fd = mkstemp(full_path_template.data());
  CF_EXPECTF(file_raw_fd >= 0, "Failed to create '{}': '{}'",
             full_path_template, strerror(errno));

  SharedFD file_fd = SharedFD::Dup(file_raw_fd);
  CF_EXPECT_EQ(close(file_raw_fd), 0, strerror(errno));

  CF_EXPECT_EQ(WriteAll(file_fd, contents), contents.size(),
               file_fd->StrError());

  CF_EXPECTF(rename(full_path_template.data(), full_path.data()) == 0,
             "Failed to rename '{}' to '{}': '{}'", full_path_template,
             full_path, strerror(errno));

  return {};
}

}  // namespace cuttlefish
