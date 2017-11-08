#include "tcp_socket.h"
#include <cerrno>

#include <cutils/sockets.h>
#define LOG_TAG "GceVNCServer"
#include <cutils/log.h>

using avd::vnc::ClientSocket;
using avd::vnc::ServerSocket;
using avd::vnc::Message;

Message ClientSocket::Recv(size_t length) {
  Message buf(length);
  ssize_t total_read = 0;
  while (total_read < static_cast<ssize_t>(length)) {
    auto just_read = read(fd_, &buf[total_read], buf.size() - total_read);
    if (just_read <= 0) {
      if (just_read < 0) {
        ALOGE("read() error: %s", strerror(errno));
      }
      other_side_closed_ = true;
      return Message{};
    }
    total_read += just_read;
  }
  ALOG_ASSERT(total_read == static_cast<ssize_t>(length));
  return buf;
}

ssize_t ClientSocket::Send(const uint8_t* data, std::size_t size) {
  std::lock_guard<std::mutex> lock(send_lock_);
  ssize_t written{};
  while (written < static_cast<ssize_t>(size)) {
    auto just_written = write(fd_, data + written, size - written);
    if (just_written <= 0) {
      ALOGI("Couldn't write to vnc client: %s", strerror(errno));
      return just_written;
    }
    written += just_written;
  }
  return written;
}

ssize_t ClientSocket::Send(const Message& message) {
  return Send(&message[0], message.size());
}

ServerSocket::ServerSocket(int port)
    : fd_{socket_inaddr_any_server(port, SOCK_STREAM)} {
  if (fd_ < 0) {
    LOG_FATAL("Couldn't open streaming server on port %d", port);
  }
}

ClientSocket ServerSocket::Accept() {
  int client = accept(fd_, nullptr, nullptr);
  if (client < 0) {
    LOG_FATAL("Error attemping to accept: %s", strerror(errno));
  }
  return ClientSocket{client};
}
