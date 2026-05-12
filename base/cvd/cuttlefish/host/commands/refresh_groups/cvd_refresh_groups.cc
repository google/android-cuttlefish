//
// Copyright (C) 2026 The Android Open Source Project
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

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * Privileged executable that calls initgroups to refresh the secondary groups
 * list. Comparable to `sg`, except that it doesn't update the primary group,
 * and refreshes the entire list of secondary groups.
 *
 * Necessary because Debian 13's `sg` no longer updates the secondary groups
 * list.
 *
 * - https://www.github.com/util-linux/util-linux/issues/4098
 * - https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1130245
 *
 * Sample invocation:
 * ```
 * cvd_refresh_groups /usr/bin/groups groups
 * ```
 */
int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage: cvd_refresh_groups <file> <argv[0]> [argv<N> ...]\n");
    return 1;
  }
  uid_t uid = getuid();
  struct passwd* pw = getpwuid(uid);
  if (!pw) {
    perror("getpwuid failed");
    return 1;
  }
  if (initgroups(pw->pw_name, getgid())) {
    perror("initgroups failed");
    return 1;
  }
  execv(argv[1], argv + 2);
  perror("execvp failed");
  return 1;
}
