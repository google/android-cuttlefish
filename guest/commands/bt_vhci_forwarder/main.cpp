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
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>
#include <iomanip>
#include <ios>
#include <optional>

#include <gflags/gflags.h>

#include "android-base/logging.h"

#include "model/hci/h4_packetizer.h"

// Copied from net/bluetooth/hci.h
#define HCI_ACLDATA_PKT 0x02
#define HCI_SCODATA_PKT 0x03
#define HCI_EVENT_PKT 0x04
#define HCI_ISODATA_PKT 0x05
#define HCI_VENDOR_PKT 0xff
#define HCI_MAX_ACL_SIZE 1024
#define HCI_MAX_FRAME_SIZE (HCI_MAX_ACL_SIZE + 4)

// Include H4 header byte, and reserve more buffer size in the case of excess
// packet.
constexpr const size_t kBufferSize = (HCI_MAX_FRAME_SIZE + 1) * 2;

constexpr const char* kVhciDev = "/dev/vhci";
DEFINE_string(virtio_console_dev, "", "virtio-console device path");

ssize_t send(int fd_, uint8_t type, const uint8_t* data, size_t length) {
  struct iovec iov[] = {{&type, sizeof(type)},
                        {const_cast<uint8_t*>(data), length}};
  ssize_t ret = 0;
  do {
    ret = TEMP_FAILURE_RETRY(writev(fd_, iov, sizeof(iov) / sizeof(iov[0])));
  } while (-1 == ret && EAGAIN == errno);
  if (ret == -1) {
    PLOG(ERROR) << "virtio-console to vhci failed";
  }
  return ret;
}

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
  if (vhci_fd < 0) {
    PLOG(ERROR) << "Unable to open " << kVhciDev;
    return -1;
  }
  int virtio_fd = open(FLAGS_virtio_console_dev.c_str(), O_RDWR);
  if (virtio_fd < 0) {
    PLOG(ERROR) << "Unable to open " << FLAGS_virtio_console_dev;
    return -1;
  }
  int set_result = setTerminalRaw(virtio_fd);
  if (set_result < 0) {
    PLOG(ERROR) << "setTerminalRaw failed " << FLAGS_virtio_console_dev;
    return -1;
  }

  struct pollfd fds[2];

  fds[0].fd = vhci_fd;
  fds[0].events = POLLIN;
  fds[1].fd = virtio_fd;
  fds[1].events = POLLIN;
  unsigned char buf[kBufferSize];

  auto h4 = rootcanal::H4Packetizer(
      virtio_fd,
      [](const std::vector<uint8_t>& /* raw_command */) {
        LOG(ERROR)
            << "Unexpected command: command pkt shouldn't be sent as response.";
      },
      [vhci_fd](const std::vector<uint8_t>& raw_event) {
        send(vhci_fd, HCI_EVENT_PKT, raw_event.data(), raw_event.size());
      },
      [vhci_fd](const std::vector<uint8_t>& raw_acl) {
        send(vhci_fd, HCI_ACLDATA_PKT, raw_acl.data(), raw_acl.size());
      },
      [vhci_fd](const std::vector<uint8_t>& raw_sco) {
        send(vhci_fd, HCI_SCODATA_PKT, raw_sco.data(), raw_sco.size());
      },
      [vhci_fd](const std::vector<uint8_t>& raw_iso) {
        send(vhci_fd, HCI_ISODATA_PKT, raw_iso.data(), raw_iso.size());
      },
      []() { LOG(INFO) << "HCI socket device disconnected"; });

  bool before_first_command = true;

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
      before_first_command = false;
    }
    if (fds[1].revents & POLLHUP) {
      LOG(ERROR) << "PollHUP";
      usleep(50 * 1000);
      continue;
    }
    if (fds[1].revents & (POLLIN | POLLERR)) {
      if (before_first_command) {
        // Drop any data left in the virtio-console from a previous reset.
        ssize_t bytes = TEMP_FAILURE_RETRY(read(virtio_fd, buf, kBufferSize));
        if (bytes < 0) {
          LOG(ERROR) << "virtio_fd ready, but read failed " << strerror(errno);
        } else {
          LOG(INFO) << "Discarding " << bytes << " bytes from virtio_fd.";
        }
        continue;
      }
      // 'virtio-console to vhci' depends on H4Packetizer because vhci expects
      // full packet, but the data from virtio-console could be partial.
      h4.OnDataReady(virtio_fd);
    }
  }
}
