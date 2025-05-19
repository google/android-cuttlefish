/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define TRACE_TAG ADB

#include "adb_utils.h"
#include "adb_unique_fd.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "adb.h"
#include "adb_trace.h"
#include "sysdeps.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include "windows.h"
#  include "shlobj.h"
#else
#include <pwd.h>
#endif


#if defined(_WIN32)
static constexpr char kNullFileName[] = "NUL";
#else
static constexpr char kNullFileName[] = "/dev/null";
#endif

void close_stdin() {
    int fd = unix_open(kNullFileName, O_RDONLY);
    if (fd == -1) {
        PLOG(FATAL) << "failed to open " << kNullFileName;
    }

    if (TEMP_FAILURE_RETRY(dup2(fd, STDIN_FILENO)) == -1) {
        PLOG(FATAL) << "failed to redirect stdin to " << kNullFileName;
    }
    unix_close(fd);
}

bool getcwd(std::string* s) {
  char* cwd = getcwd(nullptr, 0);
  if (cwd != nullptr) *s = cwd;
  free(cwd);
  return (cwd != nullptr);
}

bool directory_exists(const std::string& path) {
  struct stat sb;
  return stat(path.c_str(), &sb) != -1 && S_ISDIR(sb.st_mode);
}

std::string escape_arg(const std::string& s) {
  // Escape any ' in the string (before we single-quote the whole thing).
  // The correct way to do this for the shell is to replace ' with '\'' --- that is,
  // close the existing single-quoted string, escape a single single-quote, and start
  // a new single-quoted string. Like the C preprocessor, the shell will concatenate
  // these pieces into one string.

  std::string result;
  result.push_back('\'');

  size_t base = 0;
  while (true) {
    size_t found = s.find('\'', base);
    result.append(s, base, found - base);
    if (found == s.npos) break;
    result.append("'\\''");
    base = found + 1;
  }

  result.push_back('\'');
  return result;
}

// Given a relative or absolute filepath, create the directory hierarchy
// as needed. Returns true if the hierarchy is/was setup.
bool mkdirs(const std::string& path) {
  // TODO: all the callers do unlink && mkdirs && adb_creat ---
  // that's probably the operation we should expose.

  // Implementation Notes:
  //
  // Pros:
  // - Uses dirname, so does not need to deal with OS_PATH_SEPARATOR.
  // - On Windows, uses mingw dirname which accepts '/' and '\\', drive letters
  //   (C:\foo), UNC paths (\\server\share\dir\dir\file), and Unicode (when
  //   combined with our adb_mkdir() which takes UTF-8).
  // - Is optimistic wrt thinking that a deep directory hierarchy will exist.
  //   So it does as few stat()s as possible before doing mkdir()s.
  // Cons:
  // - Recursive, so it uses stack space relative to number of directory
  //   components.

  // If path points to a symlink to a directory, that's fine.
  struct stat sb;
  if (stat(path.c_str(), &sb) != -1 && S_ISDIR(sb.st_mode)) {
    return true;
  }

  const std::string parent(android::base::Dirname(path));

  // If dirname returned the same path as what we passed in, don't go recursive.
  // This can happen on Windows when walking up the directory hierarchy and not
  // finding anything that already exists (unlike POSIX that will eventually
  // find . or /).
  if (parent == path) {
    errno = ENOENT;
    return false;
  }

  // Recursively make parent directories of 'path'.
  if (!mkdirs(parent)) {
    return false;
  }

  // Now that the parent directory hierarchy of 'path' has been ensured,
  // create path itself.
  if (adb_mkdir(path, 0775) == -1) {
    const int saved_errno = errno;
    // If someone else created the directory, that is ok.
    if (directory_exists(path)) {
      return true;
    }
    // There might be a pre-existing file at 'path', or there might have been some other error.
    errno = saved_errno;
    return false;
  }

  return true;
}

std::string dump_hex(const void* data, size_t byte_count) {
    size_t truncate_len = 16;
    bool truncated = false;
    if (byte_count > truncate_len) {
        byte_count = truncate_len;
        truncated = true;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);

    std::string line;
    for (size_t i = 0; i < byte_count; ++i) {
        android::base::StringAppendF(&line, "%02x", p[i]);
    }
    line.push_back(' ');

    for (size_t i = 0; i < byte_count; ++i) {
        int ch = p[i];
        line.push_back(isprint(ch) ? ch : '.');
    }

    if (truncated) {
        line += " [truncated]";
    }

    return line;
}

std::string dump_header(const amessage* msg) {
    unsigned command = msg->command;
    int len = msg->data_length;
    char cmd[9];
    char arg0[12], arg1[12];
    int n;

    for (n = 0; n < 4; n++) {
        int b = (command >> (n * 8)) & 255;
        if (b < 32 || b >= 127) break;
        cmd[n] = (char)b;
    }
    if (n == 4) {
        cmd[4] = 0;
    } else {
        // There is some non-ASCII name in the command, so dump the hexadecimal value instead
        snprintf(cmd, sizeof cmd, "%08x", command);
    }

    if (msg->arg0 < 256U)
        snprintf(arg0, sizeof arg0, "%d", msg->arg0);
    else
        snprintf(arg0, sizeof arg0, "0x%x", msg->arg0);

    if (msg->arg1 < 256U)
        snprintf(arg1, sizeof arg1, "%d", msg->arg1);
    else
        snprintf(arg1, sizeof arg1, "0x%x", msg->arg1);

    return android::base::StringPrintf("[%s] arg0=%s arg1=%s (len=%d) ", cmd, arg0, arg1, len);
}

std::string dump_packet(const char* name, const char* func, const apacket* p) {
    std::string result = name;
    result += ": ";
    result += func;
    result += ": ";
    result += dump_header(&p->msg);
    result += dump_hex(p->payload.data(), p->payload.size());
    return result;
}

std::string perror_str(const char* msg) {
    return android::base::StringPrintf("%s: %s", msg, strerror(errno));
}

#if !defined(_WIN32)
// Windows version provided in sysdeps_win32.cpp
bool set_file_block_mode(borrowed_fd fd, bool block) {
    int flags = fcntl(fd.get(), F_GETFL, 0);
    if (flags == -1) {
        PLOG(ERROR) << "failed to fcntl(F_GETFL) for fd " << fd.get();
        return false;
    }
    flags = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    if (fcntl(fd.get(), F_SETFL, flags) != 0) {
        PLOG(ERROR) << "failed to fcntl(F_SETFL) for fd " << fd.get() << ", flags " << flags;
        return false;
    }
    return true;
}
#endif

bool forward_targets_are_valid(const std::string& source, const std::string& dest,
                               std::string* error) {
    if (android::base::StartsWith(source, "tcp:")) {
        // The source port may be 0 to allow the system to select an open port.
        int port;
        if (!android::base::ParseInt(&source[4], &port) || port < 0) {
            *error = android::base::StringPrintf("Invalid source port: '%s'", &source[4]);
            return false;
        }
    }

    if (android::base::StartsWith(dest, "tcp:")) {
        // The destination port must be > 0.
        int port;
        if (!android::base::ParseInt(&dest[4], &port) || port <= 0) {
            *error = android::base::StringPrintf("Invalid destination port: '%s'", &dest[4]);
            return false;
        }
    }

    return true;
}

std::string adb_get_homedir_path() {
#ifdef _WIN32
    WCHAR path[MAX_PATH];
    const HRESULT hr = SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path);
    if (FAILED(hr)) {
        D("SHGetFolderPathW failed: %s", android::base::SystemErrorCodeToString(hr).c_str());
        return {};
    }
    std::string home_str;
    if (!android::base::WideToUTF8(path, &home_str)) {
        return {};
    }
    return home_str;
#else
    if (const char* const home = getenv("HOME")) {
        return home;
    }

    struct passwd pwent;
    struct passwd* result;
    int pwent_max = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (pwent_max == -1) {
        pwent_max = 16384;
    }
    std::vector<char> buf(pwent_max);
    int rc = getpwuid_r(getuid(), &pwent, buf.data(), buf.size(), &result);
    if (rc == 0 && result) {
        return result->pw_dir;
    }

    LOG(FATAL) << "failed to get user home directory";
    return {};
#endif
}

std::string adb_get_android_dir_path() {
    std::string user_dir = adb_get_homedir_path();
    std::string android_dir = user_dir + OS_PATH_SEPARATOR + ".android";
    struct stat buf;
    if (stat(android_dir.c_str(), &buf) == -1) {
        if (adb_mkdir(android_dir, 0750) == -1) {
            PLOG(FATAL) << "Cannot mkdir '" << android_dir << "'";
        }
    }
    return android_dir;
}

std::string GetLogFilePath() {
    // https://issuetracker.google.com/112588493
    const char* path = getenv("ANDROID_ADB_LOG_PATH");
    if (path) return path;

#if defined(_WIN32)
    const char log_name[] = "adb.log";
    WCHAR temp_path[MAX_PATH];

    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa364992%28v=vs.85%29.aspx
    DWORD nchars = GetTempPathW(arraysize(temp_path), temp_path);
    if (nchars >= arraysize(temp_path) || nchars == 0) {
        // If string truncation or some other error.
        LOG(FATAL) << "cannot retrieve temporary file path: "
                   << android::base::SystemErrorCodeToString(GetLastError());
    }

    std::string temp_path_utf8;
    if (!android::base::WideToUTF8(temp_path, &temp_path_utf8)) {
        PLOG(FATAL) << "cannot convert temporary file path from UTF-16 to UTF-8";
    }

    return temp_path_utf8 + log_name;
#else
    const char* tmp_dir = getenv("TMPDIR");
    if (tmp_dir == nullptr) tmp_dir = "/tmp";
    return android::base::StringPrintf("%s/adb.%u.log", tmp_dir, getuid());
#endif
}
