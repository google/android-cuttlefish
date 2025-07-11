/*
 * Copyright (C) 2019 The Android Open Source Project
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


#include <chrono>
#include <fstream>

#include <android-base/logging.h>
#include <fmt/format.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/fs/shared_select.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/shared_fd_flag.h"
#include "cuttlefish/host/libs/config/logging.h"

namespace cuttlefish {

static uint num_tombstones_in_last_second = 0;
static std::string last_tombstone_name = "";

static std::string next_tombstone_path(const std::string& dir) {
  auto in_time = std::chrono::system_clock::now();
  auto retval = fmt::format("{}/tombstone_{:%Y-%m-%d-%H%M%S}", dir, in_time);

  // Gives tombstones unique names
  if(retval == last_tombstone_name) {
    num_tombstones_in_last_second++;
    retval += "_" + std::to_string(num_tombstones_in_last_second);
  } else {
    last_tombstone_name = retval;
    num_tombstones_in_last_second = 0;
  }

  LOG(DEBUG) << "Creating " << retval;
  return retval;
}

static constexpr size_t CHUNK_RECV_MAX_LEN = 1024;
static constexpr size_t TIMEOUT_SEC = 3;

int TombstoneReceiverMain(int argc, char** argv) {
  DefaultSubprocessLogging(argv);

  std::vector<Flag> flags;

  std::string tombstone_dir;
  flags.emplace_back(GflagsCompatFlag("tombstone_dir", tombstone_dir)
                         .Help("directory to write out tombstones in"));

  SharedFD server_fd;
  flags.emplace_back(
      SharedFDFlag("server_fd", server_fd)
          .Help("File descriptor to an already created vsock server"));

  flags.emplace_back(HelpFlag(flags));
  flags.emplace_back(UnexpectedArgumentGuard());

  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  auto parse_res = ConsumeFlags(flags, args);
  CHECK(parse_res.ok()) << "Could not process command line flags. "
                        << parse_res.error().FormatForEnv();

  CHECK(server_fd->IsOpen()) << "Did not receive a server fd";

  LOG(DEBUG) << "Host is starting server on port "
             << server_fd->VsockServerPort();

  // Server loop
  while (true) {
    auto conn = SharedFD::Accept(*server_fd);
    std::ofstream file(next_tombstone_path(tombstone_dir),
                       std::ofstream::out | std::ofstream::binary);
    auto acc = 0;
    auto cnt = 0;

    SharedFDSet read_set;
    read_set.Set(conn);

    SharedFDSet error_set;
    error_set.Set(conn);
    while (file.is_open()) {
      struct timeval timeout = {TIMEOUT_SEC, 0};
      auto val = Select(&read_set, nullptr, &error_set, &timeout);
      if (val == 0) {
        LOG(DEBUG) << "timeout";
        break;
      }
      if (read_set.IsSet(conn)) {
        char buff[CHUNK_RECV_MAX_LEN];
        auto bytes_read = conn->Recv(buff, sizeof(buff), 0);
        acc += bytes_read;
        if (bytes_read <= 0) {
          // reset the other side if it's still connected
          break;
        } else {
          file.write(buff, bytes_read);
          cnt++;
        }
      }

      if (error_set.IsSet(conn)) {
        LOG(DEBUG) << "error";
        break;
      }
    }
    LOG(DEBUG) << "done: " << acc << " bytes via " << cnt;
  }

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::TombstoneReceiverMain(argc, argv);
}
