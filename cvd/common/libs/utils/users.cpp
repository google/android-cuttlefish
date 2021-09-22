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

#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <unistd.h>
#include <grp.h>

#include <algorithm>
#include <vector>

#include <android-base/logging.h>

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

  if (std::find(groups.cbegin(), groups.cend(), gid) != groups.cend()) {
    return true;
  }
  return false;
}

} // namespace cuttlefish
