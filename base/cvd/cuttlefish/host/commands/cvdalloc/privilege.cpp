/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "cuttlefish/host/commands/cvdalloc/privilege.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#if defined(__linux__)
#include <linux/capability.h>
#include <linux/prctl.h>
#include <linux/xattr.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/xattr.h>
#endif
#include <sys/stat.h>

#include <android-base/logging.h>

#include "cuttlefish/common/libs/posix/strerror.h"

namespace cuttlefish {

#if defined(__linux__)
static int SetAmbientCapabilities() {
  /*
   * If we're on Linux, try and set capabilities instead of using setuid.
   * We need capability CAP_NET_ADMIN, but this won't normally persist
   * through exec when we shell out to invoke network commands.
   * Instead, we need to set this as an ambient capability.
   *
   * For portability reasons, run the syscall by hand and not drag in a
   * libcap dependency into bzlmod which is hard to conditionalize.
   *
   * TODO: Maybe this is neater with implementing this with netlink.
   */
  struct __user_cap_header_struct h = {_LINUX_CAPABILITY_VERSION_3, getpid()};
  struct __user_cap_data_struct data[2];

  int r = syscall(SYS_capget, &h, data);
  if (r == -1) {
    PLOG(INFO) << "SYS_capget";
    return -1;
  }

  data[0].inheritable = data[0].permitted;
  r = syscall(SYS_capset, &h, data);
  if (r == -1) {
    PLOG(INFO) << "SYS_capset";
    return -1;
  }

  r = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_ADMIN, 0, 0);
  if (r == -1) {
    PLOG(INFO) << "prctl";
    return -1;
  }

  r = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0);
  if (r == -1) {
    PLOG(INFO) << "prctl";
    return -1;
  }

  r = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_RAW, 0, 0);
  if (r == -1) {
    PLOG(INFO) << "prctl";
    return -1;
  }

  return 1;
}
#endif

Result<void> ValidateCvdallocBinary(std::string_view path) {
  struct stat st;
  int r = stat(path.data(), &st);
  CF_EXPECTF(r == 0, "Could not stat the cvdalloc binary at '{}': '{}'", path,
             StrError(errno));
#if defined(__linux__)
  /* Try and determine if the cvdalloc binary has any capabilities. */
  struct vfs_cap_data cap;
  ssize_t s = getxattr(path.data(), XATTR_NAME_CAPS, &cap, sizeof(cap));
  CF_EXPECTF(
      s != 1 && (cap.data[0].permitted & (1 << CAP_NET_ADMIN)) != 0,
      "cvdalloc binary does not have permissions to allocate resources.\n"
      "As root, please\n\n    setcap cap_net_admin,cap_net_bind_service,"
      "cap_net_raw=+ep `realpath {}`",
      path);
#else
  CF_EXPECTF(
      (st.st_mode & S_ISUID) != 0 && st.st_uid == 0,
      "cvdalloc binary does not have permissions to allocate resources.\n"
      "As root, please\n\n    chown root {}\n    chmod u+s {}\n\n"
      "and start the instance again.",
      path, path);
#endif

  return {};
}

int BeginElevatedPrivileges() {
  int r;

#if defined(__linux__)
  r = SetAmbientCapabilities();
#else
  /*
   * Explicit setuid calls seem to be required.
   *
   * This likely has something to do with invoking external commands,
   * but it isn't clear why an explicit setuid(0) is necessary.
   * It's possible a Linux kernel bug around permissions checking on tap
   * devices may be the culprit, which we can't control.
   */
  r = setuid(0);
#endif

  return r;
}

int DropPrivileges(uid_t orig) {
#if defined(__linux__)
  struct __user_cap_header_struct h = {_LINUX_CAPABILITY_VERSION_3, getpid()};
  struct __user_cap_data_struct data[2];
  data[0] = {0, 0, 0};
  data[1] = {0, 0, 0};
  int r = syscall(SYS_capset, &h, data);
  if (r == -1) {
    LOG(INFO) << "SYS_capset: " << StrError(errno);
    return -1;
  }

  r = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0L, 0L, 0L);
  if (r == -1) {
    LOG(INFO) << "prctl: " << StrError(errno);
    return -1;
  }
#endif

  return setuid(orig);
}

}  // namespace cuttlefish
