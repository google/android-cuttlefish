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
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cstddef>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

#include "common/libs/glog/logging.h"
#include "common/libs/fs/shared_select.h"

// #define ENABLE_GCE_SHARED_FD_LOGGING 1

namespace {
using cvd::SharedFDSet;

void MarkAll(const SharedFDSet& input, fd_set* dest, int* max_index) {
  for (SharedFDSet::const_iterator it = input.begin(); it != input.end();
       ++it) {
    (*it)->Set(dest, max_index);
  }
}

void CheckMarked(fd_set* in_out_mask, SharedFDSet* in_out_set) {
  if (!in_out_set) {
    return;
  }
  SharedFDSet save;
  save.swap(in_out_set);
  for (SharedFDSet::iterator it = save.begin(); it != save.end(); ++it) {
    if ((*it)->IsSet(in_out_mask)) {
      in_out_set->Set(*it);
    }
  }
}

/*
 * Android currently has host prebuilts of glibc 2.15 and 2.17, but
 * memfd_create was only added in glibc 2.27. It was defined in Linux 3.17,
 * so we consider it safe to use the low-level arbitrary syscall wrapper.
 */
#ifndef __NR_memfd_create
# if defined(__x86_64__)
#  define __NR_memfd_create 319
# elif defined(__i386__)
#  define __NR_memfd_create 356
# elif defined(__aarch64__)
#  define __NR_memfd_create 279
# else
/* No interest in other architectures. */
#  error "Unknown architecture."
# endif
#endif

int memfd_create_wrapper(const char* name, unsigned int flags) {
#ifdef CUTTLEFISH_HOST
  // TODO(schuffelen): Use memfd_create with a newer host libc.
  return syscall(__NR_memfd_create, name, flags);
#else
  return memfd_create(name, flags);
#endif
}

}  // namespace

namespace cvd {

bool FileInstance::CopyFrom(FileInstance& in, size_t length) {
  std::vector<char> buffer(8192);
  while (length > 0) {
    ssize_t num_read = in.Read(buffer.data(), std::min(buffer.size(), length));
    length -= num_read;
    if (num_read <= 0) {
      return false;
    }
    if (Write(buffer.data(), num_read) != num_read) {
      // The caller will have to log an appropriate message.
      return false;
    }
  }
  return true;
}

void FileInstance::Close() {
  std::stringstream message;
  if (fd_ == -1) {
    errno_ = EBADF;
  } else if (close(fd_) == -1) {
    errno_ = errno;
    if (identity_.size()) {
      message << __FUNCTION__ << ": " << identity_ << " failed (" << StrError() << ")";
      std::string message_str = message.str();
      Log(message_str.c_str());
    }
  } else {
    if (identity_.size()) {
      message << __FUNCTION__ << ": " << identity_ << "succeeded";
      std::string message_str = message.str();
      Log(message_str.c_str());
    }
  }
  fd_ = -1;
}

bool FileInstance::IsSet(fd_set* in) const {
  if (IsOpen() && FD_ISSET(fd_, in)) {
    return true;
  }
  return false;
}

#if ENABLE_GCE_SHARED_FD_LOGGING
void FileInstance::Log(const char* message) {
  LOG(INFO) << message;
}
#else
void FileInstance::Log(const char*) {}
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
  if (error_set) {
    MarkAll(*error_set, &errorfds, &max_index);
  }

  int rval = TEMP_FAILURE_RETRY(
      select(max_index, &readfds, &writefds, &errorfds, timeout));
  FileInstance::Log("select\n");
  CheckMarked(&readfds, read_set);
  CheckMarked(&writefds, write_set);
  CheckMarked(&errorfds, error_set);
  return rval;
}

static void MakeAddress(const char* name, bool abstract,
                        struct sockaddr_un* dest, socklen_t* len) {
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

SharedFD SharedFD::Accept(const FileInstance& listener, struct sockaddr* addr,
                          socklen_t* addrlen) {
  return SharedFD(
      std::shared_ptr<FileInstance>(listener.Accept(addr, addrlen)));
}

SharedFD SharedFD::Accept(const FileInstance& listener) {
  return SharedFD::Accept(listener, NULL, NULL);
}

SharedFD SharedFD::Dup(int unmanaged_fd) {
  int fd = fcntl(unmanaged_fd, F_DUPFD_CLOEXEC, 3);
  int error_num = errno;
  return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, error_num)));
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

SharedFD SharedFD::Event(int initval, int flags) {
  int fd = eventfd(initval, flags);
  return std::shared_ptr<FileInstance>(new FileInstance(fd, errno));
}

SharedFD SharedFD::MemfdCreate(const std::string& name, unsigned int flags) {
  int fd = memfd_create_wrapper(name.c_str(), flags);
  int error_num = errno;
  return std::shared_ptr<FileInstance>(new FileInstance(fd, error_num));
}

bool SharedFD::SocketPair(int domain, int type, int protocol,
                          SharedFD* fd0, SharedFD* fd1) {
  int fds[2];
  int rval = socketpair(domain, type, protocol, fds);
  if (rval != -1) {
    (*fd0) = std::shared_ptr<FileInstance>(new FileInstance(fds[0], errno));
    (*fd1) = std::shared_ptr<FileInstance>(new FileInstance(fds[1], errno));
    return true;
  }
  return false;
}

SharedFD SharedFD::Open(const std::string& path, int flags, mode_t mode) {
  int fd = TEMP_FAILURE_RETRY(open(path.c_str(), flags, mode));
  if (fd == -1) {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
  } else {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, 0)));
  }
}

SharedFD SharedFD::Creat(const std::string& path, mode_t mode) {
  return SharedFD::Open(path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

SharedFD SharedFD::Socket(int domain, int socket_type, int protocol) {
  int fd = TEMP_FAILURE_RETRY(socket(domain, socket_type, protocol));
  if (fd == -1) {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
  } else {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, 0)));
  }
}

SharedFD SharedFD::ErrorFD(int error) {
  return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(-1, error)));
}

SharedFD SharedFD::SocketLocalClient(const std::string& name, bool abstract,
                                     int in_type) {
  struct sockaddr_un addr;
  socklen_t addrlen;
  MakeAddress(name.c_str(), abstract, &addr, &addrlen);
  SharedFD rval = SharedFD::Socket(PF_UNIX, in_type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  if (rval->Connect(reinterpret_cast<sockaddr*>(&addr), addrlen) == -1) {
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  return rval;
}

SharedFD SharedFD::SocketLocalClient(int port, int type) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  SharedFD rval = SharedFD::Socket(AF_INET, type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  if (rval->Connect(reinterpret_cast<const sockaddr*>(&addr),
                    sizeof addr) < 0) {
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  return rval;
}

SharedFD SharedFD::SocketLocalServer(int port, int type) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  SharedFD rval = SharedFD::Socket(AF_INET, type, 0);
  if(!rval->IsOpen()) {
    return rval;
  }
  int n = 1;
  if (rval->SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
    LOG(ERROR) << "SetSockOpt failed " << rval->StrError();
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  if(rval->Bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    LOG(ERROR) << "Bind failed " << rval->StrError();
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  if (type == SOCK_STREAM) {
    if (rval->Listen(4) < 0) {
      LOG(ERROR) << "Listen failed " << rval->StrError();
      return SharedFD::ErrorFD(rval->GetErrno());
    }
  }
  return rval;
}

SharedFD SharedFD::SocketLocalServer(const std::string& name, bool abstract,
                                     int in_type, mode_t mode) {
  // DO NOT UNLINK addr.sun_path. It does NOT have to be null-terminated.
  // See man 7 unix for more details.
  if (!abstract) (void)unlink(name.c_str());

  struct sockaddr_un addr;
  socklen_t addrlen;
  MakeAddress(name.c_str(), abstract, &addr, &addrlen);
  SharedFD rval = SharedFD::Socket(PF_UNIX, in_type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }

  int n = 1;
  if (rval->SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
    LOG(ERROR) << "SetSockOpt failed " << rval->StrError();
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  if (rval->Bind(reinterpret_cast<sockaddr*>(&addr), addrlen) == -1) {
    LOG(ERROR) << "Bind failed; name=" << name << ": " << rval->StrError();
    return SharedFD::ErrorFD(rval->GetErrno());
  }

  /* Only the bottom bits are really the socket type; there are flags too. */
  constexpr int SOCK_TYPE_MASK = 0xf;

  // Connection oriented sockets: start listening.
  if ((in_type & SOCK_TYPE_MASK) == SOCK_STREAM) {
    // Follows the default from socket_local_server
    if (rval->Listen(1) == -1) {
      LOG(ERROR) << "Listen failed: " << rval->StrError();
      return SharedFD::ErrorFD(rval->GetErrno());
    }
  }

  if (!abstract) {
    if (TEMP_FAILURE_RETRY(chmod(name.c_str(), mode)) == -1) {
      LOG(ERROR) << "chmod failed: " << strerror(errno);
      // However, continue since we do have a listening socket
    }
  }
  return rval;
}

SharedFD SharedFD::VsockServer(unsigned int port, int type) {
  auto vsock = cvd::SharedFD::Socket(AF_VSOCK, type, 0);
  if (!vsock->IsOpen()) {
    return vsock;
  }
  sockaddr_vm addr{};
  addr.svm_family = AF_VSOCK;
  addr.svm_port = port;
  addr.svm_cid = VMADDR_CID_ANY;
  auto casted_addr = reinterpret_cast<sockaddr*>(&addr);
  if (vsock->Bind(casted_addr, sizeof(addr)) == -1) {
    LOG(ERROR) << "Bind failed (" << vsock->StrError() << ")";
    return SharedFD::ErrorFD(vsock->GetErrno());
  }
  if (type == SOCK_STREAM) {
    if (vsock->Listen(4) < 0) {
      LOG(ERROR) << "Listen failed (" << vsock->StrError() << ")";
      return SharedFD::ErrorFD(vsock->GetErrno());
    }
  }
  return vsock;
}

SharedFD SharedFD::VsockServer(int type) {
  return VsockServer(VMADDR_PORT_ANY, type);
}

SharedFD SharedFD::VsockClient(unsigned int cid, unsigned int port, int type) {
  auto vsock = cvd::SharedFD::Socket(AF_VSOCK, type, 0);
  if (!vsock->IsOpen()) {
    return vsock;
  }
  sockaddr_vm addr{};
  addr.svm_family = AF_VSOCK;
  addr.svm_port = port;
  addr.svm_cid = cid;
  auto casted_addr = reinterpret_cast<sockaddr*>(&addr);
  if (vsock->Connect(casted_addr, sizeof(addr)) == -1) {
    return SharedFD::ErrorFD(vsock->GetErrno());
  }
  return vsock;
}

}  // namespace cvd
