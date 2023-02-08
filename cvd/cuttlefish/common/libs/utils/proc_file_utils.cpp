/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "common/libs/utils/proc_file_utils.h"

#include <sys/stat.h>

#include <regex>
#include <sstream>
#include <string>

#include <android-base/parseint.h>

#include "common/libs/utils/files.h"

namespace cuttlefish {

// TODO(kwstephenkim): This logic is used broadly, so consider
// to create a new library.
template <typename... Args>
static std::string ConcatToString(Args&&... args) {
  std::stringstream concatenator;
  (concatenator << ... << std::forward<Args>(args));
  return concatenator.str();
}

static std::string PidDirPath(const pid_t pid) {
  return ConcatToString(kProcDir, "/", pid);
}

Result<std::vector<pid_t>> CollectPids(const uid_t uid) {
  CF_EXPECT(DirectoryExists(kProcDir));
  auto subdirs = CF_EXPECT(DirectoryContents(kProcDir));
  std::regex pid_dir_pattern("[0-9]+");
  std::vector<pid_t> pids;
  for (const auto& subdir : subdirs) {
    if (!std::regex_match(subdir, pid_dir_pattern)) {
      continue;
    }
    int pid;
    // Shouldn't failed here. If failed, either regex or
    // regex or android::base::ParseInt needs serious fixes
    CF_EXPECT(android::base::ParseInt(subdir, &pid));
    struct stat buf;
    if (::stat(PidDirPath(pid).data(), &buf) != 0) {
      continue;
    }
    if (buf.st_uid != uid) {
      continue;
    }
    pids.push_back(pid);
  }
  return pids;
}

Result<uid_t> OwnerUid(const pid_t pid) {
  auto proc_pid_path = PidDirPath(pid);
  struct stat buf;
  CF_EXPECT_EQ(::stat(proc_pid_path.data(), &buf), 0);
  return buf.st_uid;
}

}  // namespace cuttlefish
