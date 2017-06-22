/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include "common/libs/fs/shared_fd.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <cstddef>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>

#include "common/libs/auto_resources/auto_resources.h"
#include "common/libs/glog/logging.h"
#include "common/libs/fs/shared_select.h"

// #define ENABLE_GCE_SHARED_FD_LOGGING 1

namespace {
using avd::SharedFDSet;

void MarkAll(const SharedFDSet& input, fd_set* dest, int* max_index) {
  for (SharedFDSet::const_iterator it = input.begin(); it != input.end();
       ++it) {
    (*it)->Set(dest, max_index);
  }
}

void CheckMarked(fd_set* in_out_mask, fd_set* error_mask,
    SharedFDSet* in_out_set, SharedFDSet* error_set) {
  if (!in_out_set) {
    return;
  }
  SharedFDSet save;
  save.swap(in_out_set);
  for (SharedFDSet::iterator it = save.begin(); it != save.end(); ++it) {
    if (error_set && (*it)->IsSet(error_mask)) {
      error_set->Set(*it);
    }
    if ((*it)->IsSet(in_out_mask)) {
      in_out_set->Set(*it);
    }
  }
}
}  // namespace

namespace avd {

bool FileInstance::CopyFrom(FileInstance& in) {
  AutoFreeBuffer buffer;
  buffer.Resize(8192);
  while (true) {
    ssize_t num_read = in.Read(buffer.data(), buffer.size());
    if (!num_read) {
      return true;
    }
    if (num_read == -1) {
      return false;
    }
    if (num_read > 0) {
      if (Write(buffer.data(), num_read) != num_read) {
        // The caller will have to log an appropriate message.
        return false;
      }
    }
  }
  return true;
}

void FileInstance::Close() {
  AutoFreeBuffer message;
  if (fd_ == -1) {
    errno_ = EBADF;
  } else if (close(fd_) == -1) {
    errno_ = errno;
    if (identity_.size()) {
      message.PrintF("%s: %s failed (%s)", __FUNCTION__, identity_.data(),
                     StrError());
      Log(message.data());
    }
  } else {
    if (identity_.size()) {
      message.PrintF("%s: %s succeeded", __FUNCTION__, identity_.data());
      Log(message.data());
    }
  }
  fd_ = -1;
}

void FileInstance::Identify(const char* identity) {
  identity_.PrintF("fd=%d @%p is %s", fd_, this, identity);
  AutoFreeBuffer message;
  message.PrintF("%s: %s", __FUNCTION__, identity_.data());
  Log(message.data());
}

bool FileInstance::IsSet(fd_set* in) const {
  if (IsOpen() && FD_ISSET(fd_, in)) {
    return true;
  }
  return false;
}

#if ENABLE_GCE_SHARED_FD_LOGGING
void FileInstance::Log(const char* message) {
  static int log_fd = open("/dev/null", O_WRONLY|O_APPEND|O_CREAT, 0666);
  write(log_fd, message, strlen(message));
}
#else
void FileInstance::Log(const char*) { }
#endif

void FileInstance::Set(fd_set* dest, int* max_index) const {
  if (!IsOpen()) {
    return;
  }
  if (fd_ >= *max_index) {
    *max_index = fd_ + 1;
  }
  FD_SET(fd_, dest);
}


int Select(SharedFDSet* read_set, SharedFDSet* write_set,
           SharedFDSet* error_set, struct timeval* timeout) {
  int max_index = 0;
  fd_set readfds;
  FD_ZERO(&readfds);
  if (read_set) {
    MarkAll(*read_set, &readfds, &max_index);
  }
  fd_set writefds;
  FD_ZERO(&writefds);
  if (write_set) {
    MarkAll(*write_set, &writefds, &max_index);
  }
  fd_set errorfds;
  FD_ZERO(&errorfds);
  int rval = TEMP_FAILURE_RETRY(select(
      max_index, &readfds, &writefds, &errorfds, timeout));
  FileInstance::Log("select\n");
  if (error_set) {
    error_set->Zero();
  }
  CheckMarked(&readfds, &errorfds, read_set, error_set);
  CheckMarked(&writefds, &errorfds, write_set, error_set);
  return rval;
}

static void MakeAddress(
    const char* name, bool abstract, struct sockaddr_un* dest,
    socklen_t* len) {
  memset(dest, 0, sizeof(*dest));
  dest->sun_family = AF_UNIX;
  // sun_path is NOT expected to be nul-terminated.
  // See man 7 unix.
  size_t namelen;
  if (abstract) {
    // ANDROID_SOCKET_NAMESPACE_ABSTRACT
    namelen = strlen(name);
    CHECK_LE(namelen, sizeof(dest->sun_path) - 1)
        << "MakeAddress failed. Name=" << name << " is longer than allowed.";
    dest->sun_path[0] = 0;
    memcpy(dest->sun_path + 1, name, namelen);
  } else {
    // ANDROID_SOCKET_NAMESPACE_RESERVED
    // ANDROID_SOCKET_NAMESPACE_FILESYSTEM
    // TODO(pinghao): Distinguish between them?
    namelen = strlen(name);
    CHECK_LE(namelen, sizeof(dest->sun_path))
        << "MakeAddress failed. Name=" << name << " is longer than allowed.";
    strncpy(dest->sun_path, name, strlen(name));
  }
  *len = namelen + offsetof(struct sockaddr_un, sun_path) + 1;
}

SharedFD SharedFD::SocketSeqPacketServer(const char* name, mode_t mode) {
  return SocketLocalServer(name, false, SOCK_SEQPACKET, mode);
}

SharedFD SharedFD::SocketSeqPacketClient(const char* name) {
  return SocketLocalClient(name, false, SOCK_SEQPACKET);
}

SharedFD SharedFD::Accept(const FileInstance& listener,
                struct sockaddr* addr, socklen_t *addrlen) {
  return SharedFD(std::shared_ptr<FileInstance>(listener.Accept(addr, addrlen)));
}

SharedFD SharedFD::Accept(const FileInstance& listener) {
  return SharedFD::Accept(listener, NULL, NULL);
}

SharedFD SharedFD::Dup(int unmanaged_fd) {
  int fd = dup(unmanaged_fd);
  return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
}

bool SharedFD::Pipe(SharedFD* fd0, SharedFD* fd1) {
  int fds[2];
  int rval = pipe(fds);
  if (rval != -1) {
    (*fd0) = std::shared_ptr<FileInstance>(new FileInstance(fds[0], errno));
    (*fd1) = std::shared_ptr<FileInstance>(new FileInstance(fds[1], errno));
    return true;
  }
  return false;
}

SharedFD SharedFD::Event() {
  return std::shared_ptr<FileInstance>(new FileInstance(eventfd(0, 0), errno));
}

inline bool SharedFD::SocketPair(int domain, int type, int protocol, SharedFD* fd0, SharedFD* fd1){
  int fds[2];
  int rval = socketpair(domain, type, protocol, fds);
  if(rval != -1) {
    (*fd0) = std::shared_ptr<FileInstance>(new FileInstance(fds[0], errno));
    (*fd1) = std::shared_ptr<FileInstance>(new FileInstance(fds[1], errno));
    return true;
  }
  return false;
}

SharedFD SharedFD::Open(const char* path, int flags, mode_t mode) {
  int fd = TEMP_FAILURE_RETRY(open(path, flags, mode));
  if (fd == -1) {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
  } else {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, 0)));
  }
}

SharedFD SharedFD::Socket(int domain, int socket_type, int protocol) {
  int fd = TEMP_FAILURE_RETRY(socket(domain, socket_type, protocol));
  if (fd == -1) {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
  } else {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, 0)));
  }
}

SharedFD SharedFD::SocketInAddrAnyServer(int in_port, int in_type) {
  errno = 0;

  // TODO(ender): this code is very similar to SocketSeqPacketServer
  struct sockaddr_in6 addr = {
    AF_INET6,  // sin6_family
    htons(in_port),  // sin6_port
    0,  // sin6_flowinfo
    in6addr_any,  // sin6_addr
    0,  // sin6_scope_id
  };

  SharedFD rval = SharedFD::Socket(PF_INET6, in_type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }

  int n = 1;
  if (rval->SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
    LOG(ERROR) << "SetSockOpt failed " << rval->StrError();
    return SharedFD(std::shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }
  if (rval->Bind((struct sockaddr *) &addr, sizeof(addr)) == -1) {
    LOG(ERROR) << "Bind failed; port=" << in_port << ": " << rval->StrError();
    return SharedFD(std::shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }

  if (in_type == SOCK_STREAM) {
    // Follows the default from socket_local_server
    if (rval->Listen(1) == -1) {
      LOG(ERROR) << "Listen failed: " << rval->StrError();
      return SharedFD(std::shared_ptr<FileInstance>(
          new FileInstance(-1, rval->GetErrno())));
    }
  }

  return rval;
}

SharedFD SharedFD::SocketLocalClient(
    const char* name, bool abstract, int in_type) {
  struct sockaddr_un addr;
  socklen_t addrlen;
  MakeAddress(name, abstract, &addr, &addrlen);
  SharedFD rval = SharedFD::Socket(PF_UNIX, in_type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  if (rval->Connect((struct sockaddr *) &addr, addrlen) == -1) {
    LOG(ERROR) << "Connect failed; name=" << name << ": " << rval->StrError();
    return SharedFD(std::shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }
  return rval;
}

SharedFD SharedFD::SocketLocalServer(
    const char* name, bool abstract, int in_type, mode_t mode) {
  // DO NOT UNLINK addr.sun_path. It does NOT have to be null-terminated.
  // See man 7 unix for more details.
  if (!abstract) (void)unlink(name);

  struct sockaddr_un addr;
  socklen_t addrlen;
  MakeAddress(name, abstract, &addr, &addrlen);
  SharedFD rval = SharedFD::Socket(PF_UNIX, in_type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }

  int n = 1;
  if (rval->SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
    LOG(ERROR) << "SetSockOpt failed " << rval->StrError();
    return SharedFD(std::shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }
  if (rval->Bind((struct sockaddr *) &addr, addrlen) == -1) {
    LOG(ERROR) << "Bind failed; name=" << name << ": " << rval->StrError();
    return SharedFD(std::shared_ptr<FileInstance>(
        new FileInstance(-1, rval->GetErrno())));
  }

  /* Only the bottom bits are really the socket type; there are flags too. */
  constexpr int SOCK_TYPE_MASK = 0xf;

  // Connection oriented sockets: start listening.
  if ((in_type & SOCK_TYPE_MASK) == SOCK_STREAM) {
    // Follows the default from socket_local_server
    if (rval->Listen(1) == -1) {
      LOG(ERROR) << "Listen failed: " << rval->StrError();
      return SharedFD(std::shared_ptr<FileInstance>(
          new FileInstance(-1, rval->GetErrno())));
    }
  }

  if (!abstract) {
    if (TEMP_FAILURE_RETRY(chmod(name, mode)) == -1) {
      LOG(ERROR) << "chmod failed: " << strerror(errno);
      // However, continue since we do have a listening socket
    }
  }
  return rval;
}

}  // namespace avd
