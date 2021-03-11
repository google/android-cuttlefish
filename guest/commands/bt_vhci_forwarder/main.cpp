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
#include <sys/poll.h>
#include <termios.h>
#include <unistd.h>
#include <iomanip>
#include <ios>
#include <optional>

#include <gflags/gflags.h>

#include "android-base/logging.h"

// Copied from net/bluetooth/hci.h
#define HCI_VENDOR_PKT 0xff
#define HCI_MAX_ACL_SIZE 1024
#define HCI_MAX_FRAME_SIZE (HCI_MAX_ACL_SIZE + 4)

// Include H4 header byte, and reserve more buffer size in the case of excess
// packet.
constexpr const size_t kBufferSize = (HCI_MAX_FRAME_SIZE + 1) * 2;

constexpr const char* kVhciDev = "/dev/vhci";
DEFINE_string(virtio_console_dev, "", "virtio-console device path");

ssize_t forward(int from, int to, std::optional<unsigned char> filter_out,
                unsigned char* buf) {
  ssize_t count = TEMP_FAILURE_RETRY(read(from, buf, kBufferSize));
  if (count < 0) {
    PLOG(ERROR) << "read failed";
    return count;
  } else if (count == 0) {
    return count;
  }
  if (filter_out && buf[0] == *filter_out) {
    LOG(INFO) << "ignore 0x" << std::hex << std::setw(2) << std::setfill('0')
              << (unsigned)buf[0] << " packet";
    return 0;
  }
  count = TEMP_FAILURE_RETRY(write(to, buf, count));
  if (count < 0) {
    PLOG(ERROR) << "write failed, type: 0x" << std::hex << std::setw(2)
               << std::setfill('0') << (unsigned)buf[0];
  }
  return count;
}

ssize_t forward(int from, int to, unsigned char* buf) {
  return forward(from, to, std::nullopt, buf);
}

int setTerminalRaw(int fd_) {
  termios terminal_settings;
  int rval = tcgetattr(fd_, &terminal_settings);
  if (rval < 0) {
    return rval;
  }
  cfmakeraw(&terminal_settings);
  rval = tcsetattr(fd_, TCSANOW, &terminal_settings);
  return rval;
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  int vhci_fd = open(kVhciDev, O_RDWR);
  int virtio_fd = open(FLAGS_virtio_console_dev.c_str(), O_RDWR);
  setTerminalRaw(virtio_fd);

  struct pollfd fds[2];

  fds[0].fd = vhci_fd;
  fds[0].events = POLLIN;
  fds[1].fd = virtio_fd;
  fds[1].events = POLLIN;
  unsigned char buf[kBufferSize];

  while (true) {
    int ret = TEMP_FAILURE_RETRY(poll(fds, 2, -1));
    if (ret < 0) {
      PLOG(ERROR) << "poll failed";
      continue;
    }
    if (fds[0].revents & (POLLIN | POLLERR)) {
      // TODO(b/182245475) Ignore HCI_VENDOR_PKT
      // because root-canal cannot handle it.
      ssize_t c = forward(vhci_fd, virtio_fd, HCI_VENDOR_PKT, buf);
      if (c < 0) {
        PLOG(ERROR) << "vhci to virtio-console failed";
      }
    }

    if (fds[1].revents & (POLLIN | POLLERR)) {
      ssize_t c = forward(virtio_fd, vhci_fd, buf);
      if (c < 0) {
        PLOG(ERROR) << "virtio-console to vhci failed";
      }
    }
  }
}