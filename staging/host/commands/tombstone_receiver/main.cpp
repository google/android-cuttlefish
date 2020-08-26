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

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "host/libs/config/logging.h"
#include "common/libs/fs/shared_fd.h"

DEFINE_int32(
    server_fd, -1,
    "File descriptor to an already created vsock server. If negative a new "
    "server will be created at the port specified on the config file");
DEFINE_string(tombstone_dir, "", "directory to write out tombstones in");

static uint num_tombstones_in_last_second = 0;
static std::string last_tombstone_name = "";

static std::string next_tombstone_path() {
  auto in_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::stringstream ss;
  ss << FLAGS_tombstone_dir << "/tombstone_" <<
    std::put_time(std::gmtime(&in_time_t), "%Y-%m-%d-%H%M%S");
  auto retval = ss.str();

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

#define CHUNK_RECV_MAX_LEN (1024)
int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::SharedFD server_fd = cuttlefish::SharedFD::Dup(FLAGS_server_fd);
  close(FLAGS_server_fd);

  CHECK(server_fd->IsOpen()) << "Error inheriting tombstone server: "
                             << server_fd->StrError();
  LOG(DEBUG) << "Host is starting server on port "
             << server_fd->VsockServerPort();

  // Server loop
  while (true) {
    auto conn = cuttlefish::SharedFD::Accept(*server_fd);
    std::ofstream file(next_tombstone_path(),
                       std::ofstream::out | std::ofstream::binary);

    while (file.is_open()) {
      char buff[CHUNK_RECV_MAX_LEN];
      auto bytes_read = conn->Read(buff, sizeof(buff));
      if (bytes_read <= 0) {
        // reset the other side if it's still connected
        break;
      } else {
        file.write(buff, bytes_read);
      }
    }
  }

  return 0;
}
