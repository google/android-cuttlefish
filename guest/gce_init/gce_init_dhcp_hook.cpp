/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "common/libs/auto_resources/auto_resources.h"

// TODO(ghartman): Make this an alias of /init to save on disk space.
//  But watch out for the lack of make support for symlinking in JB-MR1.

void save_environment(const char* output) {
  extern char** environ;
  char** it = environ;

  if (!it) {
    printf("%s failed, environ is NULL\n", __FUNCTION__);
    return;
  }
  printf("%s: saving environment variables to %s\n", __FUNCTION__, output);
  AutoCloseFILE out(fopen(output, "w"));
  if (out.IsError()) {
    printf("%s: failed, unable to open %s for writing (%s)\n", __FUNCTION__,
           output, strerror(errno));
    return;
  }
  while (*it) {
    // Log only variables starting with lower case letters.
    // All of the interesting values from dhcpcd are lower case, and upper
    // case values tend to be things like PATH that scripts want to avoid.
    if ((**it >= 'a') && (**it <= 'z')) {
      // Quote the values to protect spaces.
      char* assignment = strchr(*it, '=');
      if (assignment) {
        fwrite(*it, 1, assignment - *it, out);
        fprintf(out, "=\"%s\"\n", assignment + 1);
      } else {
        fprintf(out, "%s\n", *it);
      }
    }
    ++it;
  }
}

int main() {
  const char* reason = getenv("reason");
  if (reason && !strcmp(reason, "BOUND")) {
    save_environment("/var/run/eth0.dhcp.env");
  }
  return 0;
}
