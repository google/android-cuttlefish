/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/utils/users.h"

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "common/libs/utils/contains.h"

namespace cuttlefish {
namespace {
gid_t GroupIdFromName(const std::string& group_name) {
  struct group grp{};
  struct group* grp_p{};
  std::vector<char> buffer(100);
  int result = 0;
  while(true) {
    result = getgrnam_r(group_name.c_str(), &grp, buffer.data(), buffer.size(),
                        &grp_p);
    if (result != ERANGE) {
      break;
    }
    buffer.resize(2*buffer.size());
  }
  if (result == 0) {
    if (grp_p != nullptr) {
      return grp.gr_gid;
    } else {
      // Caller may be checking with non-existent group name
      return -1;
    }
  } else {
    LOG(ERROR) << "Unable to get group id for group " << group_name << ": "
               << std::strerror(result);
    return -1;
  }
}

std::vector<gid_t> GetSuplementaryGroups() {
  int num_groups = getgroups(0, nullptr);
  if (num_groups < 0) {
    LOG(ERROR) << "Unable to get number of suplementary groups: "
               << std::strerror(errno);
    return {};
  }
  std::vector<gid_t> groups(num_groups + 1);
  int retval = getgroups(groups.size(), groups.data());
  if (retval < 0) {
    LOG(ERROR) << "Error obtaining list of suplementary groups (list size: "
               << groups.size() << "): " << std::strerror(errno);
    return {};
  }
  return groups;
}
}  // namespace

bool InGroup(const std::string& group) {
  auto gid = GroupIdFromName(group);
  if (gid == static_cast<gid_t>(-1)) {
    return false;
  }

  if (gid == getegid()) {
    return true;
  }

  auto groups = GetSuplementaryGroups();
  return Contains(groups, gid);
}

Result<std::string> SystemWideUserHome(const uid_t uid) {
  // getpwuid() is not thread-safe, so we need a lock across all calls
  static std::mutex getpwuid_mutex;
  std::string home_dir;
  {
    std::lock_guard<std::mutex> lock(getpwuid_mutex);
    const auto entry = getpwuid(uid);
    if (entry) {
      home_dir = entry->pw_dir;
    }
    endpwent();
    if (home_dir.empty()) {
      return CF_ERRNO("Failed to find the home directory using " << uid);
    }
  }
  std::string home_realpath;
  if (!android::base::Realpath(home_dir, &home_realpath)) {
    return CF_ERRNO("Failed to convert " << home_dir << " to its Realpath");
  }
  return home_realpath;
}

Result<std::string> SystemWideUserHome() {
  return SystemWideUserHome(getuid());
}

} // namespace cuttlefish
