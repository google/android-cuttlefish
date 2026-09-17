/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/wait_for_unix_socket.h"

#include <sched.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/wait_for_file.h"
#include "cuttlefish/files/file_is_socket.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

#ifdef __linux__
// `__SO_ACCEPTCON`, not exported to userspace. Set in `Flags` after listen(2).
constexpr unsigned long kSoAcceptCon = 0x10000;

Result<bool> IsUnixSocketListeningViaProc(const std::string& path) {
  std::ifstream proc_unix("/proc/net/unix");
  CF_EXPECTF(proc_unix.is_open(), "Failed to open /proc/net/unix: {}",
             StrError(errno));
  // Columns: Num RefCount Protocol Flags Type St Inode Path
  // Accepted sockets alias the listener's path, so check every match.
  std::string line;
  while (std::getline(proc_unix, line)) {
    std::istringstream iss(line);
    std::string num, refcount, protocol, flags, type, st, inode, socket_path;
    if (!(iss >> num >> refcount >> protocol >> flags >> type >> st >> inode >>
          socket_path)) {
      continue;
    }
    if (socket_path == path &&
        (std::strtoul(flags.c_str(), nullptr, 16) & kSoAcceptCon) != 0) {
      return true;
    }
  }
  return false;
}
#else
Result<bool> IsUnixSocketListeningViaLsof(const std::string& path) {
  static const std::regex socket_state_regex("TST=(.*)");

  Command lsof("/usr/bin/lsof");
  lsof.AddParameter(/*"format"*/ "-F", /*"connection state"*/ "TST");
  lsof.AddParameter(path);
  const std::string lsof_out = CF_EXPECT(RunAndCaptureStdout(std::move(lsof)));

  VLOG(0) << "lsof stdout:|" << lsof_out << "|";

  std::smatch socket_state_match;
  if (!std::regex_search(lsof_out, socket_state_match, socket_state_regex)) {
    return false;
  }
  return socket_state_match.size() == 2 && socket_state_match[1] == "LISTEN";
}
#endif

// Whether `path` is a listening unix socket. Must not connect: a probe would be
// consumed as the vhost-user backend's one and only frontend.
Result<bool> IsUnixSocketListening(const std::string& path) {
#ifdef __linux__
  return CF_EXPECT(IsUnixSocketListeningViaProc(path));
#else
  return CF_EXPECT(IsUnixSocketListeningViaLsof(path));
#endif
}

}  // namespace

Result<void> WaitForUnixSocket(const std::string& path, int timeoutSec) {
  const auto targetTime =
      std::chrono::system_clock::now() + std::chrono::seconds(timeoutSec);

  CF_EXPECT(WaitForFile(path, timeoutSec),
            "Waiting for socket path creation failed");
  CF_EXPECT(FileIsSocket(path), "Specified path is not a socket");

  while (true) {
    const auto currentTime = std::chrono::system_clock::now();

    if (currentTime >= targetTime) {
      return CF_ERR("Timed out");
    }

    const auto timeRemain = std::chrono::duration_cast<std::chrono::seconds>(
                                targetTime - currentTime)
                                .count();
    auto testConnect =
        SharedFD::SocketLocalClient(path, false, SOCK_STREAM, timeRemain);

    if (testConnect->IsOpen()) {
      return {};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  return CF_ERR("This shouldn't be executed");
}

Result<void> WaitForUnixSocketListeningWithoutConnect(const std::string& path,
                                                      int timeoutSec) {
  const auto targetTime =
      std::chrono::system_clock::now() + std::chrono::seconds(timeoutSec);

  CF_EXPECT(WaitForFile(path, timeoutSec),
            "Waiting for socket path creation failed");
  CF_EXPECT(FileIsSocket(path), "Specified path is not a socket");

  while (true) {
    const auto currentTime = std::chrono::system_clock::now();

    if (currentTime >= targetTime) {
      return CF_ERR("Timed out");
    }

    if (CF_EXPECT(IsUnixSocketListening(path))) {
      return {};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  return CF_ERR("This shouldn't be executed");
}

}  // namespace cuttlefish
