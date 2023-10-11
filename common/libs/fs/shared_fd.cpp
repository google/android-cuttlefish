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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstddef>

#include <algorithm>
#include <sstream>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/result.h"

// #define ENABLE_GCE_SHARED_FD_LOGGING 1

namespace cuttlefish {

namespace {

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
#ifdef __linux__
#ifdef CUTTLEFISH_HOST
  // TODO(schuffelen): Use memfd_create with a newer host libc.
  return syscall(__NR_memfd_create, name, flags);
#else
  return memfd_create(name, flags);
#endif
#else
  (void)flags;
  return shm_open(name, O_RDWR);
#endif
}

bool IsRegularFile(const int fd) {
  struct stat info;
  if (fstat(fd, &info) < 0) {
    return false;
  }
  return S_ISREG(info.st_mode);
}

constexpr size_t kPreferredBufferSize = 8192;

}  // namespace

bool FileInstance::CopyFrom(FileInstance& in, size_t length) {
  std::vector<char> buffer(kPreferredBufferSize);
  while (length > 0) {
    // Wait until either in becomes readable or our fd closes.
    constexpr ssize_t IN = 0;
    constexpr ssize_t OUT = 1;
    struct pollfd pollfds[2];
    pollfds[IN].fd = in.fd_;
    pollfds[IN].events = POLLIN;
    pollfds[IN].revents = 0;
    pollfds[OUT].fd = fd_;
    pollfds[OUT].events = 0;
    pollfds[OUT].revents = 0;
    int res = poll(pollfds, 2, -1 /* indefinitely */);
    if (res < 0) {
      errno_ = errno;
      return false;
    }
    if (pollfds[OUT].revents != 0) {
      // destination was either closed, invalid or errored, either way there is no
      // point in continuing.
      return false;
    }

    ssize_t num_read = in.Read(buffer.data(), std::min(buffer.size(), length));
    if (num_read <= 0) {
      return false;
    }
    length -= num_read;

    ssize_t written = 0;
    do {
      // No need to use poll for writes: even if the source closes, the data
      // needs to be delivered to the other side.
      auto res = Write(buffer.data(), num_read);
      if (res <= 0) {
        // The caller will have to log an appropriate message.
        return false;
      }
      written += res;
    } while(written < num_read);
  }
  return true;
}

bool FileInstance::CopyAllFrom(FileInstance& in) {
  // FileInstance may have been constructed with a non-zero errno_ value because
  // the errno variable is not zeroed out before.
  errno_ = 0;
  in.errno_ = 0;
  while (CopyFrom(in, kPreferredBufferSize)) {
  }
  // Only return false if there was an actual error.
  return !GetErrno() && !in.GetErrno();
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

bool FileInstance::Chmod(mode_t mode) {
  int original_error = errno;
  int ret = fchmod(fd_, mode);
  if (ret != 0) {
    errno_ = errno;
  }
  errno = original_error;
  return ret == 0;
}

int FileInstance::ConnectWithTimeout(const struct sockaddr* addr,
                                     socklen_t addrlen,
                                     struct timeval* timeout) {
  int original_flags = Fcntl(F_GETFL, 0);
  if (original_flags == -1) {
    LOG(ERROR) << "Could not get current file descriptor flags: " << StrError();
    return -1;
  }
  if (Fcntl(F_SETFL, original_flags | O_NONBLOCK) == -1) {
    LOG(ERROR) << "Failed to set O_NONBLOCK: " << StrError();
    return -1;
  }

  auto connect_res = Connect(
      addr, addrlen);  // This will return immediately because of O_NONBLOCK

  if (connect_res == 0) {  // Immediate success
    if (Fcntl(F_SETFL, original_flags) == -1) {
      LOG(ERROR) << "Failed to restore original flags: " << StrError();
      return -1;
    }
    return 0;
  }

  if (GetErrno() != EAGAIN && GetErrno() != EINPROGRESS) {
    LOG(DEBUG) << "Immediate connection failure: " << StrError();
    if (Fcntl(F_SETFL, original_flags) == -1) {
      LOG(ERROR) << "Failed to restore original flags: " << StrError();
    }
    return -1;
  }

  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET(fd_, &fdset);

  int select_res = select(fd_ + 1, nullptr, &fdset, nullptr, timeout);

  if (Fcntl(F_SETFL, original_flags) == -1) {
    LOG(ERROR) << "Failed to restore original flags: " << StrError();
    return -1;
  }

  if (select_res != 1) {
    LOG(ERROR) << "Did not connect within the timeout";
    return -1;
  }

  int so_error;
  socklen_t len = sizeof(so_error);
  if (GetSockOpt(SOL_SOCKET, SO_ERROR, &so_error, &len) == -1) {
    LOG(ERROR) << "Failed to get socket options: " << StrError();
    return -1;
  }

  if (so_error != 0) {
    LOG(ERROR) << "Failure in opening socket: " << so_error;
    errno_ = so_error;
    return -1;
  }
  errno_ = 0;
  return 0;
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

int SharedFD::Poll(std::vector<PollSharedFd>& fds, int timeout) {
  return Poll(fds.data(), fds.size(), timeout);
}

int SharedFD::Poll(PollSharedFd* fds, size_t num_fds, int timeout) {
  std::vector<pollfd> native_pollfds(num_fds);
  for (size_t i = 0; i < num_fds; i++) {
    native_pollfds[i].fd = fds[i].fd->fd_;
    native_pollfds[i].events = fds[i].events;
    native_pollfds[i].revents = 0;
  }
  int ret = poll(native_pollfds.data(), native_pollfds.size(), timeout);
  for (size_t i = 0; i < num_fds; i++) {
    fds[i].revents = native_pollfds[i].revents;
  }
  return ret;
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

#ifdef __linux__
SharedFD SharedFD::Event(int initval, int flags) {
  int fd = eventfd(initval, flags);
  return std::shared_ptr<FileInstance>(new FileInstance(fd, errno));
}
#endif

SharedFD SharedFD::MemfdCreate(const std::string& name, unsigned int flags) {
  int fd = memfd_create_wrapper(name.c_str(), flags);
  int error_num = errno;
  return std::shared_ptr<FileInstance>(new FileInstance(fd, error_num));
}

SharedFD SharedFD::MemfdCreateWithData(const std::string& name, const std::string& data, unsigned int flags) {
  auto memfd = MemfdCreate(name, flags);
  if (WriteAll(memfd, data) != data.size()) {
    return ErrorFD(errno);
  }
  if (memfd->LSeek(0, SEEK_SET) != 0) {
    return ErrorFD(memfd->GetErrno());
  }
  if (!memfd->Chmod(0700)) {
    return ErrorFD(memfd->GetErrno());
  }
  return memfd;
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
  return Open(path.c_str(), flags, mode);
}

SharedFD SharedFD::Open(const char* path, int flags, mode_t mode) {
  int fd = TEMP_FAILURE_RETRY(open(path, flags, mode));
  if (fd == -1) {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
  } else {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, 0)));
  }
}

SharedFD SharedFD::Creat(const std::string& path, mode_t mode) {
  return SharedFD::Open(path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

int SharedFD::Fchdir(SharedFD shared_fd) {
  if (!shared_fd.value_) {
    return -1;
  }
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(fchdir(shared_fd->fd_));
  shared_fd->errno_ = errno;
  return rval;
}

SharedFD SharedFD::Fifo(const std::string& path, mode_t mode) {
  struct stat st {};
  if (TEMP_FAILURE_RETRY(stat(path.c_str(), &st)) == 0) {
    if (TEMP_FAILURE_RETRY(remove(path.c_str())) != 0) {
      return ErrorFD(errno);
    }
  }

  int rval = TEMP_FAILURE_RETRY(mkfifo(path.c_str(), mode));
  if (rval == -1) {
    return ErrorFD(errno);
  }
  return Open(path, O_RDWR);
}

SharedFD SharedFD::Socket(int domain, int socket_type, int protocol) {
  int fd = TEMP_FAILURE_RETRY(socket(domain, socket_type, protocol));
  if (fd == -1) {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
  } else {
    return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, 0)));
  }
}

SharedFD SharedFD::Mkstemp(std::string* path) {
  int fd = mkstemp(path->data());
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
  return SocketLocalClient(name, abstract, in_type, 0);
}

SharedFD SharedFD::SocketLocalClient(const std::string& name, bool abstract,
                                     int in_type, int timeout_seconds) {
  struct sockaddr_un addr;
  socklen_t addrlen;
  MakeAddress(name.c_str(), abstract, &addr, &addrlen);
  SharedFD rval = SharedFD::Socket(PF_UNIX, in_type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  struct timeval timeout = {timeout_seconds, 0};
  auto casted_addr = reinterpret_cast<sockaddr*>(&addr);
  if (rval->ConnectWithTimeout(casted_addr, addrlen, &timeout) == -1) {
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  return rval;
}

SharedFD SharedFD::SocketLocalClient(int port, int type) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  auto rval = SharedFD::Socket(AF_INET, type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  if (rval->Connect(reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  return rval;
}

SharedFD SharedFD::SocketClient(const std::string& host, int port, int type,
                                std::chrono::seconds timeout) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(host.c_str());
  auto rval = SharedFD::Socket(AF_INET, type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }
  struct timeval timeout_timeval = {static_cast<time_t>(timeout.count()), 0};
  if (rval->ConnectWithTimeout(reinterpret_cast<const sockaddr*>(&addr),
                               sizeof addr, &timeout_timeval) < 0) {
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  return rval;
}

SharedFD SharedFD::Socket6Client(const std::string& host, const std::string& interface,
                                 int port, int type, std::chrono::seconds timeout) {
  sockaddr_in6 addr{};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);
  inet_pton(AF_INET6, host.c_str(), &addr.sin6_addr);
  auto rval = SharedFD::Socket(AF_INET6, type, 0);
  if (!rval->IsOpen()) {
    return rval;
  }

  if (!interface.empty()) {
#ifdef __linux__
    ifreq ifr{};
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", interface.c_str());

    if (rval->SetSockOpt(SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) == -1) {
      return SharedFD::ErrorFD(rval->GetErrno());
    }
#elif defined(__APPLE__)
    int idx = if_nametoindex(interface.c_str());
    if (rval->SetSockOpt(IPPROTO_IP, IP_BOUND_IF, &idx, sizeof(idx)) == -1) {
      return SharedFD::ErrorFD(rval->GetErrno());
    }
#else
#error "Unsupported operating system"
#endif
  }

  struct timeval timeout_timeval = {static_cast<time_t>(timeout.count()), 0};
  if (rval->ConnectWithTimeout(reinterpret_cast<const sockaddr*>(&addr),
                               sizeof addr, &timeout_timeval) < 0) {
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  return rval;
}

SharedFD SharedFD::SocketLocalServer(int port, int type) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
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
  if (type == SOCK_STREAM || type == SOCK_SEQPACKET) {
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
  auto socket_type = in_type & SOCK_TYPE_MASK;

  // Connection oriented sockets: start listening.
  if (socket_type == SOCK_STREAM || socket_type == SOCK_SEQPACKET) {
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

#ifdef __linux__
SharedFD SharedFD::VsockServer(unsigned int port, int type, unsigned int cid) {
  auto vsock = SharedFD::Socket(AF_VSOCK, type, 0);
  if (!vsock->IsOpen()) {
    return vsock;
  }
  sockaddr_vm addr{};
  addr.svm_family = AF_VSOCK;
  addr.svm_port = port;
  addr.svm_cid = cid;
  auto casted_addr = reinterpret_cast<sockaddr*>(&addr);
  if (vsock->Bind(casted_addr, sizeof(addr)) == -1) {
    LOG(ERROR) << "Port " << port << " Bind failed (" << vsock->StrError()
               << ")";
    return SharedFD::ErrorFD(vsock->GetErrno());
  }
  if (type == SOCK_STREAM || type == SOCK_SEQPACKET) {
    if (vsock->Listen(4) < 0) {
      LOG(ERROR) << "Port" << port << " Listen failed (" << vsock->StrError()
                 << ")";
      return SharedFD::ErrorFD(vsock->GetErrno());
    }
  }
  return vsock;
}

SharedFD SharedFD::VsockServer(int type) {
  return VsockServer(VMADDR_PORT_ANY, type);
}

SharedFD SharedFD::VsockClient(unsigned int cid, unsigned int port, int type) {
  auto vsock = SharedFD::Socket(AF_VSOCK, type, 0);
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
#endif

SharedFD WeakFD::lock() const {
  auto locked_file_instance = value_.lock();
  if (locked_file_instance) {
    return SharedFD(locked_file_instance);
  }
  return SharedFD();
}

ScopedMMap::ScopedMMap(void* ptr, size_t len) : ptr_(ptr), len_(len) {}

ScopedMMap::ScopedMMap() : ptr_(MAP_FAILED), len_(0) {}

ScopedMMap::ScopedMMap(ScopedMMap&& other)
    : ptr_(other.ptr_), len_(other.len_) {
  other.ptr_ = MAP_FAILED;
  other.len_ = 0;
}

ScopedMMap::~ScopedMMap() {
  if (ptr_ != MAP_FAILED) {
    munmap(ptr_, len_);
  }
}

/* static */ std::shared_ptr<FileInstance> FileInstance::ClosedInstance() {
  return std::shared_ptr<FileInstance>(new FileInstance(-1, EBADF));
}

int FileInstance::Bind(const struct sockaddr* addr, socklen_t addrlen) {
  errno = 0;
  int rval = bind(fd_, addr, addrlen);
  errno_ = errno;
  return rval;
}

int FileInstance::Connect(const struct sockaddr* addr, socklen_t addrlen) {
  errno = 0;
  int rval = connect(fd_, addr, addrlen);
  errno_ = errno;
  return rval;
}

int FileInstance::UNMANAGED_Dup() {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(dup(fd_));
  errno_ = errno;
  return rval;
}

int FileInstance::UNMANAGED_Dup2(int newfd) {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(dup2(fd_, newfd));
  errno_ = errno;
  return rval;
}

int FileInstance::Fcntl(int command, int value) {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(fcntl(fd_, command, value));
  errno_ = errno;
  return rval;
}

int FileInstance::Fsync() {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(fsync(fd_));
  errno_ = errno;
  return rval;
}

Result<void> FileInstance::Flock(int operation) {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(flock(fd_, operation));
  errno_ = errno;
  CF_EXPECT(rval == 0, StrError());
  return {};
}

int FileInstance::GetSockName(struct sockaddr* addr, socklen_t* addrlen) {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(getsockname(fd_, addr, addrlen));
  if (rval == -1) {
    errno_ = errno;
  }
  return rval;
}

#ifdef __linux__
unsigned int FileInstance::VsockServerPort() {
  struct sockaddr_vm vm_socket;
  socklen_t length = sizeof(vm_socket);
  GetSockName(reinterpret_cast<struct sockaddr*>(&vm_socket), &length);
  return vm_socket.svm_port;
}
#endif

int FileInstance::Ioctl(int request, void* val) {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(ioctl(fd_, request, val));
  errno_ = errno;
  return rval;
}

int FileInstance::LinkAtCwd(const std::string& path) {
  std::string name = "/proc/self/fd/";
  name += std::to_string(fd_);
  errno = 0;
  int rval =
      linkat(-1, name.c_str(), AT_FDCWD, path.c_str(), AT_SYMLINK_FOLLOW);
  errno_ = errno;
  return rval;
}

int FileInstance::Listen(int backlog) {
  errno = 0;
  int rval = listen(fd_, backlog);
  errno_ = errno;
  return rval;
}

off_t FileInstance::LSeek(off_t offset, int whence) {
  errno = 0;
  off_t rval = TEMP_FAILURE_RETRY(lseek(fd_, offset, whence));
  errno_ = errno;
  return rval;
}

ssize_t FileInstance::Recv(void* buf, size_t len, int flags) {
  errno = 0;
  ssize_t rval = TEMP_FAILURE_RETRY(recv(fd_, buf, len, flags));
  errno_ = errno;
  return rval;
}

ssize_t FileInstance::RecvMsg(struct msghdr* msg, int flags) {
  errno = 0;
  ssize_t rval = TEMP_FAILURE_RETRY(recvmsg(fd_, msg, flags));
  errno_ = errno;
  return rval;
}

ssize_t FileInstance::Read(void* buf, size_t count) {
  errno = 0;
  ssize_t rval = TEMP_FAILURE_RETRY(read(fd_, buf, count));
  errno_ = errno;
  return rval;
}

#ifdef __linux__
int FileInstance::EventfdRead(eventfd_t* value) {
  errno = 0;
  auto rval = eventfd_read(fd_, value);
  errno_ = errno;
  return rval;
}
#endif

ssize_t FileInstance::Send(const void* buf, size_t len, int flags) {
  errno = 0;
  ssize_t rval = TEMP_FAILURE_RETRY(send(fd_, buf, len, flags));
  errno_ = errno;
  return rval;
}

ssize_t FileInstance::SendMsg(const struct msghdr* msg, int flags) {
  errno = 0;
  ssize_t rval = TEMP_FAILURE_RETRY(sendmsg(fd_, msg, flags));
  errno_ = errno;
  return rval;
}

int FileInstance::Shutdown(int how) {
  errno = 0;
  int rval = shutdown(fd_, how);
  errno_ = errno;
  return rval;
}

int FileInstance::SetSockOpt(int level, int optname, const void* optval,
                             socklen_t optlen) {
  errno = 0;
  int rval = setsockopt(fd_, level, optname, optval, optlen);
  errno_ = errno;
  return rval;
}

int FileInstance::GetSockOpt(int level, int optname, void* optval,
                             socklen_t* optlen) {
  errno = 0;
  int rval = getsockopt(fd_, level, optname, optval, optlen);
  errno_ = errno;
  return rval;
}

int FileInstance::SetTerminalRaw() {
  errno = 0;
  termios terminal_settings;
  int rval = tcgetattr(fd_, &terminal_settings);
  errno_ = errno;
  if (rval < 0) {
    return rval;
  }
  cfmakeraw(&terminal_settings);
  rval = tcsetattr(fd_, TCSANOW, &terminal_settings);
  errno_ = errno;
  return rval;
}

std::string FileInstance::StrError() const {
  errno = 0;
  return std::string(strerror(errno_));
}

ScopedMMap FileInstance::MMap(void* addr, size_t length, int prot, int flags,
                              off_t offset) {
  errno = 0;
  auto ptr = mmap(addr, length, prot, flags, fd_, offset);
  errno_ = errno;
  return ScopedMMap(ptr, length);
}

ssize_t FileInstance::Truncate(off_t length) {
  errno = 0;
  ssize_t rval = TEMP_FAILURE_RETRY(ftruncate(fd_, length));
  errno_ = errno;
  return rval;
}

ssize_t FileInstance::Write(const void* buf, size_t count) {
  if (count == 0 && !IsRegular()) {
    return 0;
  }
  errno = 0;
  ssize_t rval = TEMP_FAILURE_RETRY(write(fd_, buf, count));
  errno_ = errno;
  return rval;
}

#ifdef __linux__
int FileInstance::EventfdWrite(eventfd_t value) {
  errno = 0;
  int rval = eventfd_write(fd_, value);
  errno_ = errno;
  return rval;
}
#endif

bool FileInstance::IsATTY() {
  errno = 0;
  int rval = isatty(fd_);
  errno_ = errno;
  return rval;
}

int FileInstance::Futimens(const struct timespec times[2]) {
  errno = 0;
  int rval = TEMP_FAILURE_RETRY(futimens(fd_, times));
  errno_ = errno;
  return rval;
}

#ifdef __linux__
Result<std::string> FileInstance::ProcFdLinkTarget() const {
  std::stringstream output_composer;
  output_composer << "/proc/" << getpid() << "/fd/" << fd_;
  const std::string mem_fd_link = output_composer.str();
  std::string mem_fd_target;
  CF_EXPECT(
      android::base::Readlink(mem_fd_link, &mem_fd_target),
      "Getting link for the memory file \"" << mem_fd_link << "\" failed");
  return mem_fd_target;
}
#endif

FileInstance::FileInstance(int fd, int in_errno)
    : fd_(fd), errno_(in_errno), is_regular_file_(IsRegularFile(fd_)) {
  // Ensure every file descriptor managed by a FileInstance has the CLOEXEC
  // flag
  TEMP_FAILURE_RETRY(fcntl(fd, F_SETFD, FD_CLOEXEC));
  std::stringstream identity;
  identity << "fd=" << fd << " @" << this;
  identity_ = identity.str();
}

FileInstance* FileInstance::Accept(struct sockaddr* addr,
                                   socklen_t* addrlen) const {
  int fd = TEMP_FAILURE_RETRY(accept(fd_, addr, addrlen));
  if (fd == -1) {
    return new FileInstance(fd, errno);
  } else {
    return new FileInstance(fd, 0);
  }
}

}  // namespace cuttlefish
