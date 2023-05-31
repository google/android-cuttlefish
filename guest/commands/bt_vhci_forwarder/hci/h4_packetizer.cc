//
// Copyright 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "h4_packetizer.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>

#include "log.h"

namespace rootcanal {

H4Packetizer::H4Packetizer(int fd, PacketReadCallback command_cb,
                           PacketReadCallback event_cb,
                           PacketReadCallback acl_cb, PacketReadCallback sco_cb,
                           PacketReadCallback iso_cb,
                           ClientDisconnectCallback disconnect_cb)
    : uart_fd_(fd),
      h4_parser_(command_cb, event_cb, acl_cb, sco_cb, iso_cb),
      disconnect_cb_(std::move(disconnect_cb)) {}

size_t H4Packetizer::Send(uint8_t type, const uint8_t* data, size_t length) {
  struct iovec iov[] = {{&type, sizeof(type)},
                        {const_cast<uint8_t*>(data), length}};
  ssize_t ret = 0;
  do {
    ret = writev(uart_fd_, iov, sizeof(iov) / sizeof(iov[0]));
  } while (-1 == ret && (EINTR == errno || EAGAIN == errno));

  if (ret == -1) {
    LOG_ERROR("Error writing to UART (%s)", strerror(errno));
  } else if (ret < static_cast<ssize_t>(length + 1)) {
    LOG_ERROR("%d / %d bytes written - something went wrong...",
              static_cast<int>(ret), static_cast<int>(length + 1));
  }
  return ret;
}

void H4Packetizer::OnDataReady(int fd) {
  if (disconnected_) {
    return;
  }
  ssize_t bytes_to_read = h4_parser_.BytesRequested();
  std::vector<uint8_t> buffer(bytes_to_read);

  ssize_t bytes_read;
  do {
    bytes_read = read(fd, buffer.data(), bytes_to_read);
  } while (bytes_read == -1 && errno == EINTR);

  if (bytes_read == 0) {
    LOG_INFO("remote disconnected!");
    disconnected_ = true;
    disconnect_cb_();
    return;
  }
  if (bytes_read < 0) {
    if (errno == EAGAIN) {
      // No data, try again later.
      return;
    }
    if (errno == ECONNRESET) {
      // They probably rejected our packet
      disconnected_ = true;
      disconnect_cb_();
      return;
    }

    LOG_ALWAYS_FATAL("Read error in %d: %s", h4_parser_.CurrentState(),
                     strerror(errno));
  }
  h4_parser_.Consume(buffer.data(), bytes_read);
}

}  // namespace rootcanal
