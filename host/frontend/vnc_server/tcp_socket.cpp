#include "tcp_socket.h"
#include <cerrno>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#define LOG_TAG "GceVNCServer"
#include <glog/logging.h>

using avd::SharedFD;
using avd::vnc::ClientSocket;
using avd::vnc::Message;
using avd::vnc::ServerSocket;

Message ClientSocket::Recv(size_t length) {
  Message buf(length);
  ssize_t total_read = 0;
  while (total_read < static_cast<ssize_t>(length)) {
    auto just_read = fd_->Read(&buf[total_read], buf.size() - total_read);
    if (just_read <= 0) {
      if (just_read < 0) {
        LOG(ERROR) << "read() error: " << strerror(errno);
      }
      other_side_closed_ = true;
      return Message{};
    }
    total_read += just_read;
  }
  CHECK(total_read == static_cast<ssize_t>(length));
  return buf;
}

ssize_t ClientSocket::Send(const uint8_t* data, std::size_t size) {
  std::lock_guard<std::mutex> lock(send_lock_);
  ssize_t written{};
  while (written < static_cast<ssize_t>(size)) {
    auto just_written = fd_->Write(data + written, size - written);
    if (just_written <= 0) {
      LOG(INFO) << "Couldn't write to vnc client: " << strerror(errno);
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
    : fd_{SharedFD::SocketLocalServer(port, SOCK_STREAM)} {
  if (!fd_->IsOpen()) {
    LOG(FATAL) << "Couldn't open streaming server on port " << port;
  }
}

ClientSocket ServerSocket::Accept() {
  SharedFD client = SharedFD::Accept(*fd_);
  if (!client->IsOpen()) {
    LOG(FATAL) << "Error attemping to accept: " << strerror(errno);
  }
  return ClientSocket{client};
}
