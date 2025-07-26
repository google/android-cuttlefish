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

#include <chrono>
#include <regex>
#include <string>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/common/libs/utils/wait_for_file.h"

namespace cuttlefish {

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

    sched_yield();
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

  std::regex socket_state_regex("TST=(.*)");

  while (true) {
    const auto currentTime = std::chrono::system_clock::now();

    if (currentTime >= targetTime) {
      return CF_ERR("Timed out");
    }

    Command lsof("/usr/bin/lsof");
    lsof.AddParameter(/*"format"*/ "-F", /*"connection state"*/ "TST");
    lsof.AddParameter(path);
    std::string lsof_out;
    std::string lsof_err;
    int rval =
        RunWithManagedStdio(std::move(lsof), nullptr, &lsof_out, &lsof_err);
    if (rval != 0) {
      return CF_ERR("Failed to run `lsof`, stderr: " << lsof_err);
    }

    LOG(DEBUG) << "lsof stdout:|" << lsof_out << "|";
    LOG(DEBUG) << "lsof stderr:|" << lsof_err << "|";

    std::smatch socket_state_match;
    if (std::regex_search(lsof_out, socket_state_match, socket_state_regex)) {
      if (socket_state_match.size() == 2) {
        const std::string& socket_state = socket_state_match[1];
        if (socket_state == "LISTEN") {
          return {};
        }
      }
    }

    sched_yield();
  }

  return CF_ERR("This shouldn't be executed");
}

}
