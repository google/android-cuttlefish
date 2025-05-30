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

#include "sysdeps.h"

#include <sys/utsname.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

bool set_tcp_keepalive(borrowed_fd fd, int interval_sec) {
    int enable = (interval_sec > 0);
    if (adb_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable))) {
        return false;
    }

    if (!enable) {
        return true;
    }

    // Idle time before sending the first keepalive is TCP_KEEPIDLE on Linux, TCP_KEEPALIVE on Mac.
#if defined(TCP_KEEPIDLE)
    if (adb_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &interval_sec, sizeof(interval_sec))) {
        return false;
    }
#elif defined(TCP_KEEPALIVE)
    if (adb_setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &interval_sec, sizeof(interval_sec))) {
        return false;
    }
#endif

    // TCP_KEEPINTVL and TCP_KEEPCNT are available on Linux 2.4+ and OS X 10.8+ (Mountain Lion).
#if defined(TCP_KEEPINTVL)
    if (adb_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval_sec, sizeof(interval_sec))) {
        return false;
    }
#endif

#if defined(TCP_KEEPCNT)
    // On Windows this value is hardcoded to 10. This is a reasonable value, so we do the same here
    // to match behavior. See SO_KEEPALIVE documentation at
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ee470551(v=vs.85).aspx.
    const int keepcnt = 10;
    if (adb_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt))) {
        return false;
    }
#endif

    return true;
}

static __inline__ void disable_close_on_exec(borrowed_fd fd) {
    const auto oldFlags = fcntl(fd.get(), F_GETFD);
    const auto newFlags = (oldFlags & ~FD_CLOEXEC);
    if (newFlags != oldFlags) {
        fcntl(fd.get(), F_SETFD, newFlags);
    }
}

Process adb_launch_process(std::string_view executable, std::vector<std::string> args,
                           std::initializer_list<int> fds_to_inherit) {
    const auto pid = fork();
    if (pid != 0) {
        // parent, includes the case when failed to fork()
        return Process(pid);
    }
    // child
    std::vector<std::string> copies;
    copies.reserve(args.size() + 1);
    copies.emplace_back(executable);
    copies.insert(copies.end(), std::make_move_iterator(args.begin()),
                  std::make_move_iterator(args.end()));

    std::vector<char*> rawArgs;
    rawArgs.reserve(copies.size() + 1);
    for (auto&& str : copies) {
        rawArgs.push_back(str.data());
    }
    rawArgs.push_back(nullptr);
    for (auto fd : fds_to_inherit) {
        disable_close_on_exec(fd);
    }
    exit(execv(copies.front().data(), rawArgs.data()));
}

// For Unix variants (Linux, OSX), the underlying uname() system call
// is utilized to extract out a version string comprising of:
// 1.) "Linux" or "Darwin"
// 2.) OS system release (e.g. "5.19.11")
// 3.) machine (e.g. "x86_64")
// like: "Linux 5.19.11-1<snip>1-amd64 (x86_64)"
std::string GetOSVersion(void) {
    utsname name;
    uname(&name);

    return android::base::StringPrintf("%s %s (%s)", name.sysname, name.release, name.machine);
}

std::optional<ssize_t> network_peek(borrowed_fd fd) {
    ssize_t upper_bound_bytes;
#if defined(__APPLE__)
    // Can't use recv(MSG_TRUNC) (not supported).
    // Can't use ioctl(FIONREAD) (returns size in socket queue instead next message size).
    socklen_t optlen = sizeof(upper_bound_bytes);
    if (getsockopt(fd.get(), SOL_SOCKET, SO_NREAD, &upper_bound_bytes, &optlen) == -1) {
        upper_bound_bytes = -1;
    }
#else
    upper_bound_bytes = recv(fd.get(), nullptr, 0, MSG_PEEK | MSG_TRUNC);
#endif
    if (upper_bound_bytes == -1) {
        PLOG(ERROR) << "network_peek error";
    }
    return upper_bound_bytes == -1 ? std::nullopt : std::make_optional(upper_bound_bytes);
}