#include "host/ivserver/socketutils.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>

#include <glog/logging.h>

#define LOG_TAG "ivserver::socketutils"

namespace ivserver {

int start_listener_socket(const std::string &path) {
  int fd = -1;
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    LOG(ERROR) << "Unable to create UNIX Domain socket.";
    return -1;
  }

  address.sun_family = AF_UNIX;
  strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

  if (bind(fd, reinterpret_cast<struct sockaddr *>(&address),
           sizeof(address)) == -1) {
    LOG(ERROR) << "bind failed. Please remove exisiting socket and retry.";
    close(fd);
    return -1;
  }

  if (listen(fd, 1) == -1) {
    LOG(ERROR) << "listen failed.";
    close(fd);
    return -1;
  }

  return fd;
}

int connect_to_socket(const std::string &path) {
  int fd = 1;
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    LOG(ERROR) << "Unable to create UNIX Domain socket.";
    return -1;
  }

  address.sun_family = AF_UNIX;
  strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

  if (connect(fd, reinterpret_cast<struct sockaddr *>(&address),
              sizeof(address)) == -1) {
    LOG(ERROR) << "connect failed.";
    close(fd);
    return -1;
  }

  return fd;
}

int handle_new_connection(const int uds, const bool blocking) {
  int fd = -1;
  struct sockaddr_un sockaddr;
  socklen_t socket_len;

  socket_len = sizeof(sockaddr);
  fd = accept(uds, reinterpret_cast<struct sockaddr *>(&sockaddr), &socket_len);
  if (fd == -1) {
    LOG(ERROR) << "accept failed.";
    return -1;
  }

  if (!blocking) {
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
      LOG(ERROR) << "couldn't set socket to non-blocking mode.";
      return -1;
    }
  }
  return fd;
}

static bool _send_msg(const int uds, const void *const buf,
                      const uint32_t len) {
  uint32_t offset = 0;
  int sent;
  while (len - offset) {
    sent = send(uds, reinterpret_cast<const char *>(buf) + offset, len - offset,
                0);
    if (sent == -1) {
      LOG(ERROR) << "_send_msg failed.";
      return false;
    }
    assert(static_cast<uint32_t>(sent) <= len);
    offset += sent;
  }

  return true;
}

template <typename T>
bool send_msg(const int uds, const T &msg) {
  return _send_msg(uds, &msg, sizeof(msg));
}

template bool send_msg<uint16_t>(const int, const uint16_t &);
template bool send_msg<int32_t>(const int, const int32_t &);
template bool send_msg<uint32_t>(const int, const uint32_t &);
template bool send_msg<uint64_t>(const int, const uint64_t &);

template <>
bool send_msg<std::string>(const int uds, const std::string &data) {
  return _send_msg(uds, data.c_str(), data.length());
}

//
// This is loosely based on ivshmem-server.
//
bool send_msg(const int uds, const int fd, uint64_t data) {
  struct msghdr msg;
  struct iovec vec[1];
  union {
    struct cmsghdr cmsg;
    char control_data[CMSG_SPACE(sizeof(fd))];
  } control_msg;

  memset(&msg, 0, sizeof(msghdr));
  memset(&vec[0], 0, sizeof(vec[0]));
  memset(&control_msg, 0, sizeof(control_msg));

  vec[0].iov_base = reinterpret_cast<void *>(&data);
  vec[0].iov_len = sizeof(data);

  struct cmsghdr *cmsg;
  msg.msg_iov = vec;
  msg.msg_iovlen = 1;
  msg.msg_control = &control_msg;
  msg.msg_controllen = sizeof(control_msg);
  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
  memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

  if (sendmsg(uds, &msg, 0) == -1) {
    LOG(ERROR) << "Error sending control msg.";
    return false;
  }

  return true;
}

//
// This is loosely based on ivshmem-server.
//
int recv_msg(const int uds, uint64_t *data) {
  int retval = -1;
  struct msghdr msg;
  struct iovec vec[1];

  union {
    struct cmsghdr cmsg;
    char control[CMSG_SPACE(sizeof(*data))];
  } control_msg;

  memset(&msg, 0, sizeof(msghdr));
  memset(&vec[0], 0, sizeof(vec[0]));
  memset(&control_msg, 0, sizeof(control_msg));

  vec[0].iov_base = data;
  vec[0].iov_len = sizeof(*data);
  struct cmsghdr *cmsg;
  msg.msg_iov = vec;
  msg.msg_iovlen = 1;
  msg.msg_control = &control_msg;
  msg.msg_controllen = sizeof(control_msg);

  auto ret = recvmsg(uds, &msg, 0);
  if (ret < sizeof(*data)) {
    LOG(ERROR) << "error in receiving control_message.";
    close(uds);
    return -1;
  }

  for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
        cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
      continue;
    }
    memcpy(&retval, CMSG_DATA(cmsg), sizeof(int));
    return retval;
  }

  return retval;
}

std::shared_ptr<std::string> recv_msg(const int uds, const uint32_t len) {
  char *buffer = new char[len + 1];
  if (!buffer) {
    return nullptr;
  }

  memset(buffer, 0, len + 1);

  uint32_t received = 0;
  int got;
  while (received < len) {
    got = recv(uds, &buffer[received], len - received, 0);
    if (got == -1) {
      LOG(ERROR) << "recv error";
      delete[] buffer;
      return nullptr;
    }
    received += got;
  }

  std::string data_string = std::string(buffer);
  auto retval = std::make_shared<std::string>(data_string);
  delete[] buffer;
  return retval;
}

static std::shared_ptr<char> recv_msg_bytes(const int uds, const uint32_t len) {
  char *buffer = new char[len];
  if (!buffer) {
    return nullptr;
  }

  memset(buffer, 0, len);

  uint32_t received = 0;

  int got;
  while (received < len) {
    got = recv(uds, &buffer[received], len - received, 0);
    if (got == -1) {
      LOG(ERROR) << "recv_msg_bytes error.";
      delete[] buffer;
      return nullptr;
    }
    received += got;
  }

  std::shared_ptr<char> retval(buffer, std::default_delete<char[]>());
  return retval;
}

int16_t recv_msg_int16(const int uds) {
  std::shared_ptr<char> data = recv_msg_bytes(uds, sizeof(int16_t));
  if (!data) {
    return -1;
  }

  return *reinterpret_cast<int16_t *>(data.get());
}

int32_t recv_msg_int32(const int uds) {
  std::shared_ptr<char> data = recv_msg_bytes(uds, sizeof(int32_t));
  if (!data) {
    return -1;
  }
  return *reinterpret_cast<int32_t *>(data.get());
}

int64_t recv_msg_int64(const int uds) {
  std::shared_ptr<char> data = recv_msg_bytes(uds, sizeof(int64_t));
  if (!data) {
    return -1;
  }
  return *reinterpret_cast<int64_t *>(data.get());
}

}  // namespace ivserver
