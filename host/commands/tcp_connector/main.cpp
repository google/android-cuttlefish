/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <ios>
#include <mutex>

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <thread>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"

DEFINE_int32(fifo_in, -1, "A pipe for incoming communication");
DEFINE_int32(fifo_out, -1, "A pipe for outgoing communication");
DEFINE_int32(data_port, -1, "A port for datas");
DEFINE_int32(buffer_size, -1, "The buffer size");

void openSocket(cuttlefish::SharedFD* fd, int port) {
  static std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);
  *fd = cuttlefish::SharedFD::SocketLocalClient(port, SOCK_STREAM);
}

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  auto fifo_in = cuttlefish::SharedFD::Dup(FLAGS_fifo_in);
  if (!fifo_in->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_fifo_in << ": "
               << fifo_in->StrError();
    return 1;
  }
  close(FLAGS_fifo_in);

  auto fifo_out = cuttlefish::SharedFD::Dup(FLAGS_fifo_out);
  if (!fifo_out->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_fifo_out << ": "
               << fifo_out->StrError();
    return 1;
  }
  close(FLAGS_fifo_out);
  cuttlefish::SharedFD sock;
  openSocket(&sock, FLAGS_data_port);

  auto guest_to_host = std::thread([&]() {
    while (true) {
      char buf[FLAGS_buffer_size];
      auto read = fifo_in->Read(buf, sizeof(buf));
      while (cuttlefish::WriteAll(sock, buf, read) == -1) {
        LOG(ERROR) << "failed to write to socket, retry.";
        // Wait for the host process to be ready
        sleep(1);
        openSocket(&sock, FLAGS_data_port);
      }
    }
  });

  auto host_to_guest = std::thread([&]() {
    while (true) {
      char buf[FLAGS_buffer_size];
      auto read = sock->Read(buf, sizeof(buf));
      if (read == -1) {
        LOG(ERROR) << "failed to read from socket, retry.";
        // Wait for the host process to be ready
        sleep(1);
        openSocket(&sock, FLAGS_data_port);
        continue;
      }
      cuttlefish::WriteAll(fifo_out, buf, read);
    }
  });
  guest_to_host.join();
  host_to_guest.join();
}
