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
DEFINE_int32(data_port, -1, "A port for data");
DEFINE_int32(buffer_size, -1, "The buffer size");
DEFINE_int32(dump_packet_size, -1,
             "Dump incoming/outgoing packets up to given size");

void OpenSocket(cuttlefish::SharedFD* fd, int port) {
  static std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);
  for (;;) {
    *fd = cuttlefish::SharedFD::SocketLocalClient(port, SOCK_STREAM);
    if ((*fd)->IsOpen()) {
      return;
    }
    LOG(ERROR) << "Failed to open socket: " << (*fd)->StrError();
    // Wait a little and try again
    sleep(1);
  }
}

void DumpPackets(const char* prefix, char* buf, int size) {
  if (FLAGS_dump_packet_size < 0 || size <= 0) {
    return;
  }
  char bytes_string[1001] = {0};
  int len = FLAGS_dump_packet_size < size ? FLAGS_dump_packet_size : size;
  for (int i = 0; i < len; i++) {
    if ((i + 1) * 5 > sizeof(bytes_string)) {
      // Buffer out of bounds
      break;
    }
    sprintf(bytes_string + (i * 5), "0x%02x ", buf[i]);
  }
  if (len < size) {
    LOG(DEBUG) << prefix << ": sz=" << size << ", first " << len << " bytes=["
               << bytes_string << "...]";
  } else {
    LOG(DEBUG) << prefix << ": sz=" << size << ", bytes=[" << bytes_string
               << "]";
  }
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
  OpenSocket(&sock, FLAGS_data_port);

  auto guest_to_host = std::thread([&]() {
    while (true) {
      char buf[FLAGS_buffer_size];
      auto read = fifo_in->Read(buf, sizeof(buf));
      if (read < 0) {
        LOG(WARNING) << "Error reading from guest: " << fifo_in->StrError();
        sleep(1);
        continue;
      }
      DumpPackets("Read from FIFO", buf, read);
      while (cuttlefish::WriteAll(sock, buf, read) == -1) {
        LOG(WARNING) << "Failed to write to host socket (will retry): "
                     << sock->StrError();
        // Wait for the host process to be ready
        sleep(1);
        OpenSocket(&sock, FLAGS_data_port);
      }
    }
  });

  auto host_to_guest = std::thread([&]() {
    while (true) {
      char buf[FLAGS_buffer_size];
      auto read = sock->Read(buf, sizeof(buf));
      DumpPackets("Read from socket", buf, read);
      if (read == -1) {
        LOG(WARNING) << "Failed to read from host socket (will retry): "
                     << sock->StrError();
        // Wait for the host process to be ready
        sleep(1);
        OpenSocket(&sock, FLAGS_data_port);
        continue;
      }
      auto wrote = cuttlefish::WriteAll(fifo_out, buf, read);
      if (wrote < 0) {
        LOG(WARNING) << "Failed to write to guest: " << fifo_out->StrError();
        sleep(1);
        continue;
      }
    }
  });
  guest_to_host.join();
  host_to_guest.join();
}
