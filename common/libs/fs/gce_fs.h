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
#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_FS_GCE_FS_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_FS_GCE_FS_H_

#include <sys/types.h>
#include <unistd.h>

/*
 * TEMP_FAILURE_RETRY is defined by some, but not all, versions of
 * <unistd.h>. (Alas, it is not as standard as we'd hoped!) So, if it's
 * not already defined, then define it here.
 */
#ifndef GCE_TEMP_FAILURE_RETRY
/* Used to retry syscalls that can return EINTR. */
#define GCE_TEMP_FAILURE_RETRY(exp) ({         \
    decltype (exp) _rc;                      \
    do {                                   \
        _rc = (exp);                       \
    } while (_rc == -1 && errno == EINTR); \
    _rc; })
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ensure that directory exists with given mode and owners.
 */
extern int gce_fs_prepare_dir(const char* path, mode_t mode, uid_t uid, gid_t gid);

/*
 * Ensure that all directories along given path exist, creating parent
 * directories as needed.  Validates that given path is absolute and that
 * it contains no relative "." or ".." paths or symlinks.  Last path segment
 * is treated as filename and ignored, unless the path ends with "/".
 */
extern int gce_fs_mkdirs(const char* path, mode_t mode);

#define EPHEMERAL_FS_BLOCK_DIR "/ephemeral_store"

#ifdef __cplusplus
}
#endif

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_FS_GCE_FS_H_
