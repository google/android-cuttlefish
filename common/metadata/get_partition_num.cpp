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
#include "common/metadata/get_partition_num.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "common/auto_resources/auto_resources.h"

namespace {
const char kDefaultPartitionsPath[] = "/partitions";
}  // namespace

// Looks up the partition number for a given name.
// Returns -1 if the partition number cannot be found for any reason.
long GetPartitionNum(const char* name, const char* path) {
  if (!path) {
    path = kDefaultPartitionsPath;
  }
  size_t name_len = strlen(name);
  AutoCloseFILE f(fopen(path, "r"));
  if (f.IsError()) {
    printf("%s: fopen(%s) failed %s:%d (%s)", __FUNCTION__,
               path, __FILE__, __LINE__, strerror(errno));
    return -1;
  }
  char line[160];
  while (!f.IsEOF()) {
    if (!fgets(line, sizeof(line), f)) {
      if (!f.IsEOF()) {
        printf("%s: fgets failed %s:%d (%s)", __FUNCTION__,
                   __FILE__, __LINE__, strerror(errno));
      }
      return -1;
    }
    if (!strncmp(name, line, name_len) && (line[name_len] == ' ')) {
      char* end;
      const char* base = line + name_len + 1;
      long rval = strtol(base, &end, 10);
      if (base != end) {
        return rval;
      } else {
        printf("%s: parse failed line=%s %s:%d", __FUNCTION__,
                   line, __FILE__, __LINE__);
      }
    }
  }
  printf("%s: Could not find name=%s %s:%d", __FUNCTION__, name,
             __FILE__, __LINE__);
  return -1;
}
