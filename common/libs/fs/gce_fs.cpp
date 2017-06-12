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
#define LOG_TAG "gce_cutils"

/* These defines are only needed because prebuilt headers are out of date */
#define __USE_XOPEN2K8 1
#define _ATFILE_SOURCE 1
#define _GNU_SOURCE 1

#include "common/libs/fs/gce_fs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>

#include <glog/logging.h>

#define ALL_PERMS (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)
#define BUF_SIZE 64

int gce_fs_prepare_dir(const char* path, mode_t mode, uid_t uid, gid_t gid) {
  // Check if path needs to be created
  struct stat sb;
  if (GCE_TEMP_FAILURE_RETRY(lstat(path, &sb)) == -1) {
    if (errno == ENOENT) {
      goto create;
    } else {
      LOG(ERROR) << "Failed to lstat(" << path << "): " << strerror(errno);
      return -1;
    }
  }

  // Exists, verify status
  if (!S_ISDIR(sb.st_mode)) {
    LOG(ERROR) << "Not a directory: " << path;
    return -1;
  }
  if (((sb.st_mode & ALL_PERMS) == mode) && (sb.st_uid == uid) && (sb.st_gid == gid)) {
    return 0;
  } else {
    goto fixup;
  }

create:
  if (GCE_TEMP_FAILURE_RETRY(mkdir(path, mode)) == -1) {
    if (errno != EEXIST) {
      LOG(ERROR) << "Failed to mkdir(" << path << "): " << strerror(errno);
      return -1;
    }
  }

fixup:
  if (GCE_TEMP_FAILURE_RETRY(chmod(path, mode)) == -1) {
    LOG(ERROR) << "Failed to chmod(" << path << ", " << mode << "): "
               << strerror(errno);
    return -1;
  }
  if (GCE_TEMP_FAILURE_RETRY(chown(path, uid, gid)) == -1) {
    LOG(ERROR) << "Failed to chown(" << path << ", " << uid << ", " << gid
               << "): " << strerror(errno);
    return -1;
  }

  return 0;
}

int gce_fs_mkdirs(const char* path, mode_t mode) {
  struct stat info;
  char* buf;
  int len;
  int offset;

  if (!path) {
    LOG(ERROR) << "Path is NULL";
    return -EINVAL;
  }
  len = strlen(path);
  if (len < 1) {
    LOG(ERROR) << "Path is empty.";
    return -EINVAL;
  }
  buf = strdup(path);
  if (buf[0] != '/') {
    LOG(ERROR) << "Path must be absolute: " << buf;
    goto error_exit;
  }
  // because there's no need to create /, offset starts from 1.
  for (offset = 1; offset < len; offset++) {
    if (buf[offset] == '/' || offset == (len - 1)) {
      if (buf[offset] == '/') {
        buf[offset] = '\0';
      }
      if (stat(buf, &info) != 0) {
        LOG(INFO) << "mkdir " << buf;
        mode_t saved_umask = umask(0);
        if (mkdir(buf, mode) == -1) {
          umask(saved_umask);
          LOG(ERROR) << "Can't create a directory " << buf
                     << ": " << strerror(errno);
          goto error_exit;
        }
        umask(saved_umask);
      } else if ((info.st_mode & S_IFDIR) == 0) {
        LOG(ERROR) << "path is not valid; a file exists at " << buf;
        goto error_exit;
      }
      if (buf[offset] == '\0') {
        buf[offset] = '/';
      }
    }
  }
  free(buf);
  return 0;

error_exit:
  free(buf);
  return -EINVAL;
}

