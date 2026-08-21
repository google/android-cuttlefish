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
#include "cuttlefish/common/libs/fs/fd.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "fmt/chrono.h"  // IWYU pragma: keep
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

// #define ENABLE_GCE_SHARED_FD_LOGGING 1

namespace cuttlefish {

namespace {

class LocalErrno {
 public:
  LocalErrno(int& local_errno) : local_errno_(local_errno), preserved_(errno) {
    errno = 0;
  }
  ~LocalErrno() {
    local_errno_ = errno;
    errno = preserved_;
  }

 private:
  int& local_errno_;
  int preserved_;
};

Result<int> MemfdCreateWrapper(const std::string& name, unsigned int flags) {
#ifdef __linux__
  int fd = TEMP_FAILURE_RETRY(memfd_create(name.c_str(), flags));
  CF_EXPECTF(fd >= 0, "memfd_create('{}', {}) failed: {}", name, flags,
             StrError(errno));
#else
  (void)flags;
  int fd = TEMP_FAILURE_RETRY(shm_open(name.c_str(), O_RDWR));
  CF_EXPECTF(fd >= 0, "shm_open('{}', O_RDWR) failed: {}", name,
             StrError(errno));
#endif
  return fd;
}

bool IsRegularFile(const int fd) {
  struct stat info;
  if (fstat(fd, &info) < 0) {
    return false;
  }
  return S_ISREG(info.st_mode);
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

constexpr size_t kPreferredBufferSize = 8192;

}  // namespace

Fd::Fd() : Fd(-1, 0) {}

Fd::Fd(Fd&& other) : Fd() {
  std::swap(fd_, other.fd_);
  std::swap(errno_, other.errno_);
}

Fd::~Fd() { Close(); }

Fd& Fd::operator=(Fd&& other) {
  Close();
  std::swap(fd_, other.fd_);
  std::swap(errno_, other.errno_);
  return *this;
}

Result<Fd> Fd::Accept(const Fd& listener) {
  const int fd = TEMP_FAILURE_RETRY(accept(listener.fd_, nullptr, nullptr));
  CF_EXPECTF(fd >= 0, "accept(..., nullptr, nullptr) failed: '{}",
             ::cuttlefish::StrError(errno));
  return Fd(fd, 0);
}

Result<Fd> Fd::Dup(int unmanaged_fd) {
  const int fd = TEMP_FAILURE_RETRY(fcntl(unmanaged_fd, F_DUPFD_CLOEXEC, 3));
  CF_EXPECTF(fd >= 0, "fcntl(..., F_DUPFD_CLOEXEC, 3) failed: {}",
             ::cuttlefish::StrError(errno));
  return Fd(fd, 0);
}

Result<std::pair<Fd, Fd>> Fd::Pipe() {
  int fds[2];
#ifdef __linux__
  const int rval = TEMP_FAILURE_RETRY(pipe2(fds, O_CLOEXEC));
  CF_EXPECTF(rval != -1, "pipe2(..., O_CLOEXEC) failed: {}",
             ::cuttlefish::StrError(errno));
#else
  const int rval = TEMP_FAILURE_RETRY(pipe(fds));
  CF_EXPECTF(rval != -1, "pipe(...) failed: {}", ::cuttlefish::StrError(errno));
#endif
  return std::make_pair(Fd(fds[0], 0), Fd(fds[1], 0));
}

#ifdef __linux__
Result<Fd> Fd::Event(int initval, int flags) {
  int fd = TEMP_FAILURE_RETRY(eventfd(initval, flags));
  CF_EXPECTF(fd >= 0, "eventfd({}, {}) failed: {}", initval, flags,
             ::cuttlefish::StrError(errno));
  return Fd(fd, 0);
}

Result<Fd> Fd::ShmOpen(std::string_view name, int oflag, int mode) {
  std::string name_str(name);
  const int fd = TEMP_FAILURE_RETRY(shm_open(name_str.c_str(), oflag, mode));
  CF_EXPECTF(fd >= 0, "shm_open('{}', {}, {}) failed: {}", name, oflag, mode,
             ::cuttlefish::StrError(errno));
  return Fd(fd, 0);
}
#endif

Result<Fd> Fd::MemfdCreate(std::string_view name, unsigned int flags) {
  return Fd(CF_EXPECT(MemfdCreateWrapper(std::string(name), flags)), 0);
}

Result<std::pair<Fd, Fd>> Fd::SocketPair(int domain, int type, int protocol) {
  int fds[2];
  int rval = TEMP_FAILURE_RETRY(socketpair(domain, type, protocol, fds));
  CF_EXPECTF(rval != -1, "socketpair({}, {}, {}) failed: {}", domain, type,
             protocol, ::cuttlefish::StrError(errno));
  return std::make_pair(Fd(fds[0], 0), Fd(fds[1], 0));
}

Result<Fd> Fd::Open(std::string_view path, int flags, mode_t mode) {
  const int fd =
      TEMP_FAILURE_RETRY(open(std::string(path).c_str(), flags, mode));
  CF_EXPECTF(fd >= 0, "open('{}', {}, {}) failed: {}", path, flags, mode,
             ::cuttlefish::StrError(errno));
  return Fd(fd, 0);
}

Result<Fd> Fd::InotifyFd(void) {
  const int fd = TEMP_FAILURE_RETRY(inotify_init1(IN_CLOEXEC));
  CF_EXPECTF(fd >= 0, "inotify_init1(IN_CLOEXEC) failed: {}",
             ::cuttlefish::StrError(errno));
  return Fd(fd, 0);
}

Result<Fd> Fd::Creat(std::string_view path, mode_t mode) {
  return CF_EXPECT(Fd::Open(path, O_CREAT | O_WRONLY | O_TRUNC, mode));
}

Result<Fd> Fd::Fifo(std::string_view path, mode_t mode) {
  struct stat st{};
  std::string path_str(path);
  if (TEMP_FAILURE_RETRY(stat(path_str.c_str(), &st)) == 0) {
    CF_EXPECTF(TEMP_FAILURE_RETRY(remove(path_str.c_str())) == 0,
               "Failed to delete old file at '{}': '{}'", path,
               ::cuttlefish::StrError(errno));
  }

  CF_EXPECTF(TEMP_FAILURE_RETRY(mkfifo(path_str.c_str(), mode)) == 0,
             "Failed to mkfifo('{}', {:o})", path, mode);
  return CF_EXPECT(Open(path, O_RDWR));
}

Result<Fd> Fd::Socket(int domain, int socket_type, int protocol) {
  const int fd = TEMP_FAILURE_RETRY(socket(domain, socket_type, protocol));
  CF_EXPECTF(fd >= 0, "socket({}, {}, {}) failed: {}", domain, socket_type,
             protocol, ::cuttlefish::StrError(errno));
  return Fd(fd, 0);
}

Result<std::pair<Fd, std::string>> Fd::Mkostemp(const std::string_view path,
                                                const int flags) {
  // mkostemp replaces the Xs with random selections to make a unique filename
  std::string temp_path = fmt::format("{}XXXXXX", path);
  const int fd = TEMP_FAILURE_RETRY(mkostemp(temp_path.data(), flags));
  CF_EXPECTF(fd != -1, "mkostemp('{}', {}) failed: {}", path, flags,
             ::cuttlefish::StrError(errno));
  return std::make_pair<Fd, std::string>(Fd(fd, 0), std::move(temp_path));
}

Fd Fd::ErrorFD(int error) { return Fd(-1, error); }

Result<Fd> Fd::SocketLocalClient(std::string_view name, bool abstract,
                                 int in_type) {
  return CF_EXPECT(SocketLocalClient(name, abstract, in_type, 0));
}

Result<Fd> Fd::SocketLocalClient(std::string_view name, bool abstract,
                                 int in_type, int timeout_seconds) {
  std::string name_str(name);

  struct sockaddr_un addr;
  socklen_t addrlen;
  MakeAddress(name_str.c_str(), abstract, &addr, &addrlen);
  Fd rval = CF_EXPECT(Fd::Socket(PF_UNIX, in_type, 0));

  struct timeval timeout = {timeout_seconds, 0};
  auto casted_addr = reinterpret_cast<sockaddr*>(&addr);
  CF_EXPECTF(rval.ConnectWithTimeout(casted_addr, addrlen, &timeout) != -1,
             "ConnectWithTimeout failed: {}", rval.StrError());
  return rval;
}

Result<Fd> Fd::SocketLocalClient(int port, int type) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  Fd rval = CF_EXPECT(Fd::Socket(AF_INET, type, 0));

  auto addr_ptr = reinterpret_cast<const sockaddr*>(&addr);
  CF_EXPECTF(rval.Connect(addr_ptr, sizeof addr) >= 0,
             "Connect failed to port {} with type {}: {}", port, type,
             rval.StrError());

  return rval;
}

Result<Fd> Fd::SocketClient(std::string_view host, int port, int type,
                            std::chrono::seconds timeout) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(std::string(host).c_str());
  Fd rval = CF_EXPECT(Fd::Socket(AF_INET, type, 0));

  struct timeval timeout_tval = {static_cast<time_t>(timeout.count()), 0};
  auto addr_ptr = reinterpret_cast<const sockaddr*>(&addr);
  CF_EXPECTF(
      rval.ConnectWithTimeout(addr_ptr, sizeof addr, &timeout_tval) >= 0,
      "ConnectWithTimeout to host {} and port {} with type {} failed in {}: {}",
      host, port, type, timeout, rval.StrError());
  return rval;
}

Result<Fd> Fd::Socket6Client(std::string_view host, std::string_view interface,
                             int port, int type, std::chrono::seconds timeout) {
  sockaddr_in6 addr{};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);
  inet_pton(AF_INET6, std::string(host).c_str(), &addr.sin6_addr);
  Fd rval = CF_EXPECT(Fd::Socket(AF_INET6, type, 0));
  if (!rval.IsOpen()) {
    return rval;
  }

  if (!interface.empty()) {
#ifdef __linux__
    ifreq ifr{};
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s",
             std::string(interface).c_str());

    CF_EXPECTF(
        rval.SetSockOpt(SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) >= 0,
        "SetSockOpt(SOL_SOCKET, SO_BINDTODEVICE, ...) failed: {}",
        rval.StrError());
#elif defined(__APPLE__)
    int idx = if_nametoindex(std::string(interface).c_str());
    CF_EXPECTF(rval.SetSockOpt(IPPROTO_IP, IP_BOUND_IF, &idx, sizeof(idx)) >= 0,
               "SetSockOpt(IPPROTO_IP, IP_BOUND_IF, {}, ...) failed: {}", idx,
               rval.StrError());
#else
#error "Unsupported operating system"
#endif
  }

  struct timeval timeout_timeval = {static_cast<time_t>(timeout.count()), 0};
  CF_EXPECTF(rval.ConnectWithTimeout(reinterpret_cast<const sockaddr*>(&addr),
                                     sizeof addr, &timeout_timeval) >= 0,
             "ConnectWithTimeout failed: {}", rval.StrError());
  return rval;
}

Result<Fd> Fd::SocketLocalServer(int port, int type) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  Fd rval = CF_EXPECT(Fd::Socket(AF_INET, type, 0));

  int n = 1;
  CF_EXPECTF(rval.SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) >= 0,
             "SetSockOpt failed: {}", rval.StrError());

  CF_EXPECTF(rval.Bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) >= 0,
             "Bind failed: {}", rval.StrError());

  if (type == SOCK_STREAM || type == SOCK_SEQPACKET) {
    CF_EXPECTF(rval.Listen(4) >= 0, "Listen failed: {}", rval.StrError());
  }
  return rval;
}

Result<Fd> Fd::SocketLocalServer(std::string_view name, bool abstract,
                                 int in_type, mode_t mode) {
  // DO NOT UNLINK addr.sun_path. It does NOT have to be null-terminated.
  // See man 7 unix for more details.
  std::string name_str(name);
  if (!abstract) {
    (void)TEMP_FAILURE_RETRY(unlink(name_str.c_str()));
  }

  struct sockaddr_un addr;
  socklen_t addrlen;
  MakeAddress(name_str.c_str(), abstract, &addr, &addrlen);
  Fd rval = CF_EXPECT(Fd::Socket(PF_UNIX, in_type, 0));

  int n = 1;
  CF_EXPECTF(rval.SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) >= 0,
             "SetSockOpt failed: {}", rval.StrError());
  CF_EXPECTF(rval.Bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) >= 0,
             "Bind failed: {}", rval.StrError());

  /* Only the bottom bits are really the socket type; there are flags too. */
  constexpr int SOCK_TYPE_MASK = 0xf;
  auto socket_type = in_type & SOCK_TYPE_MASK;

  // Connection oriented sockets: start listening.
  if (socket_type == SOCK_STREAM || socket_type == SOCK_SEQPACKET) {
    // Follows the default from socket_local_server
    CF_EXPECTF(rval.Listen(4) >= 0, "Listen failed: {}", rval.StrError());
  }

  if (!abstract) {
    if (TEMP_FAILURE_RETRY(chmod(name_str.c_str(), mode)) == -1) {
      LOG(ERROR) << "chmod failed: " << ::cuttlefish::StrError(errno);
      // However, continue since we do have a listening socket
    }
  }
  return rval;
}

#ifdef __linux__
Result<Fd> Fd::VsockServer(unsigned int port, int type,
                           std::optional<int> vhost_user_vsock_listening_cid,
                           unsigned int cid) {
  if (vhost_user_vsock_listening_cid) {
    return CF_EXPECT(Fd::SocketLocalServer(
        GetVhostUserVsockServerAddr(port, *vhost_user_vsock_listening_cid),
        false /* abstract */, type, 0666 /* mode */));
  }

  Fd vsock = CF_EXPECT(Fd::Socket(AF_VSOCK, type, 0));

  sockaddr_vm addr{};
  addr.svm_family = AF_VSOCK;
  addr.svm_port = port;
  addr.svm_cid = cid;
  auto casted_addr = reinterpret_cast<sockaddr*>(&addr);
  CF_EXPECTF(vsock.Bind(casted_addr, sizeof(addr)) >= 0,
             "Bind failed port {}: {}", port, vsock.StrError());

  if (type == SOCK_STREAM || type == SOCK_SEQPACKET) {
    CF_EXPECTF(vsock.Listen(4) >= 0, "Listen on port {} failed: {}", port,
               vsock.StrError());
  }
  return vsock;
}

Result<Fd> Fd::VsockServer(int type,
                           std::optional<int> vhost_user_vsock_listening_cid) {
  return CF_EXPECT(
      VsockServer(VMADDR_PORT_ANY, type, vhost_user_vsock_listening_cid));
}

std::string Fd::GetVhostUserVsockServerAddr(
    unsigned int port, int vhost_user_vsock_listening_cid) {
  // TODO(b/277909042): better path than /tmp/vsock_{}/vm.vsock_{}
  return fmt::format(
      "{}_{}", GetVhostUserVsockClientAddr(vhost_user_vsock_listening_cid),
      port);
}

std::string Fd::GetVhostUserVsockClientAddr(int cid) {
  // TODO(b/277909042): better path than /tmp/vsock_{}/vm.vsock_{}
  return fmt::format("{}/vsock_{}_{}/vm.vsock", TempDir(), cid, getuid());
}

#endif

bool Fd::CopyFrom(Fd& in, size_t length, Fd* stop) {
  LocalErrno record_errno(errno_);
  std::vector<char> buffer(kPreferredBufferSize);
  while (length > 0) {
    int nfds = stop == nullptr ? 2 : 3;
    // Wait until either in becomes readable or our fd closes.
    constexpr ssize_t IN = 0;
    constexpr ssize_t OUT = 1;
    constexpr ssize_t STOP = 2;
    struct pollfd pollfds[3];
    pollfds[IN].fd = in.fd_;
    pollfds[IN].events = POLLIN;
    pollfds[IN].revents = 0;
    pollfds[OUT].fd = fd_;
    pollfds[OUT].events = 0;
    pollfds[OUT].revents = 0;
    if (stop) {
      pollfds[STOP].fd = stop->fd_;
      pollfds[STOP].events = POLLIN;
      pollfds[STOP].revents = 0;
    }
    if (poll(pollfds, nfds, -1 /* indefinitely */) < 0) {
      return false;
    }
    if (stop && pollfds[STOP].revents & POLLIN) {
      return false;
    }
    if (pollfds[OUT].revents != 0) {
      // destination was either closed, invalid or errored, either way there is
      // no point in continuing.
      return false;
    }

    Result<size_t> num_read =
        in.Read(buffer.data(), std::min(buffer.size(), length));
    if (num_read.value_or(0) == 0) {
      return false;
    }
    length -= *num_read;

    ssize_t written = 0;
    do {
      // No need to use poll for writes: even if the source closes, the data
      // needs to be delivered to the other side.
      Result<uint64_t> res = Write(buffer.data(), *num_read);
      if (res.value_or(0) == 0) {
        // The caller will have to log an appropriate message.
        return false;
      }
      written += *res;
    } while (written < *num_read);
  }
  return true;
}

bool Fd::CopyAllFrom(Fd& in, Fd* stop) {
  // Fd may have been constructed with a non-zero errno_ value because
  // the errno variable is not zeroed out before.
  errno_ = 0;
  in.errno_ = 0;
  while (CopyFrom(in, kPreferredBufferSize, stop)) {
  }
  // Only return false if there was an actual error.
  return !GetErrno() && !in.GetErrno();
}

bool Fd::SendFile(Fd& in, off_t* offset, size_t count) {
  LocalErrno record_errno(errno_);
  while (count > 0) {
#ifdef __linux__
    const auto bytes_written =
        TEMP_FAILURE_RETRY(sendfile(fd_, in.fd_, offset, count));
    if (bytes_written <= 0) {
      return false;
    }
#elif defined(__APPLE__)
    off_t bytes_written = count;
    auto success = TEMP_FAILURE_RETRY(
        sendfile(in.fd_, fd_, *offset, &bytes_written, nullptr, 0));
    *offset += bytes_written;
    if (success < 0 || bytes_written == 0) {
      return false;
    }
#endif
    count -= bytes_written;
  }
  return true;
}

void Fd::Close() {
  std::stringstream message;
  if (fd_ == -1) {
    errno_ = EBADF;
  } else if (close(fd_) == -1) {
    errno_ = errno;
    if (!identity_.empty()) {
      message << __FUNCTION__ << ": " << identity_ << " failed (" << StrError()
              << ")";
      std::string message_str = message.str();
      Log(message_str.c_str());
    }
  } else {
    if (!identity_.empty()) {
      message << __FUNCTION__ << ": " << identity_ << "succeeded";
      std::string message_str = message.str();
      Log(message_str.c_str());
    }
  }
  fd_ = -1;
}

bool Fd::Chmod(mode_t mode) {
  LocalErrno record_errno(errno_);

  return fchmod(fd_, mode) == 0;
}

int Fd::ConnectWithTimeout(const struct sockaddr* addr, socklen_t addrlen,
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
    VLOG(0) << "Immediate connection failure: " << StrError();
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

bool Fd::IsSet(fd_set* in) const {
  if (IsOpen() && FD_ISSET(fd_, in)) {
    return true;
  }
  return false;
}

#if ENABLE_GCE_SHARED_FD_LOGGING
void Fd::Log(const char* message) { LOG(INFO) << message; }
#else
void Fd::Log(const char*) {}
#endif

void Fd::Set(fd_set* dest, int* max_index) const {
  if (!IsOpen()) {
    return;
  }
  if (fd_ >= *max_index) {
    *max_index = fd_ + 1;
  }
  FD_SET(fd_, dest);
}

int Fd::Bind(const struct sockaddr* addr, socklen_t addrlen) {
  LocalErrno record_errno(errno_);

  return bind(fd_, addr, addrlen);
}

int Fd::Connect(const struct sockaddr* addr, socklen_t addrlen) {
  LocalErrno record_errno(errno_);

  return connect(fd_, addr, addrlen);
}

int Fd::UNMANAGED_Dup() {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(dup(fd_));
}

int Fd::UNMANAGED_Dup2(int newfd) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(dup2(fd_, newfd));
}

int Fd::Fchdir() {
  if (fd_ < 0) {
    return -1;
  }
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(fchdir(fd_));
}

int Fd::Fcntl(int command, int value) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(fcntl(fd_, command, value));
}

int Fd::Fsync() {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(fsync(fd_));
}

Result<void> Fd::Flock(int operation) {
  LocalErrno record_errno(errno_);

  CF_EXPECT(TEMP_FAILURE_RETRY(flock(fd_, operation)) == 0,
            ::cuttlefish::StrError(errno));
  return {};
}

Result<struct stat> Fd::Fstat() {
  LocalErrno record_errno(errno_);

  struct stat file_info = {};
  CF_EXPECT(TEMP_FAILURE_RETRY(fstat(fd_, &file_info)) == 0,
            ::cuttlefish::StrError(errno));
  return file_info;
}

int Fd::GetSockName(struct sockaddr* addr, socklen_t* addrlen) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(getsockname(fd_, addr, addrlen));
}

#ifdef __linux__
unsigned int Fd::VsockServerPort() {
  struct sockaddr_vm vm_socket;
  socklen_t length = sizeof(vm_socket);
  GetSockName(reinterpret_cast<struct sockaddr*>(&vm_socket), &length);
  return vm_socket.svm_port;
}
#endif

int Fd::Ioctl(int request, void* val) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(ioctl(fd_, request, val));
}

int Fd::LinkAtCwd(const std::string& path) {
  LocalErrno record_errno(errno_);

  std::string name = "/proc/self/fd/";
  name += std::to_string(fd_);
  return linkat(AT_FDCWD, name.c_str(), AT_FDCWD, path.c_str(),
                AT_SYMLINK_FOLLOW);
}

int Fd::Listen(int backlog) {
  LocalErrno record_errno(errno_);

  return listen(fd_, backlog);
}

off_t Fd::LSeek(off_t offset, int whence) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(lseek(fd_, offset, whence));
}

ssize_t Fd::Recv(void* buf, size_t len, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(recv(fd_, buf, len, flags));
}

ssize_t Fd::RecvMsg(struct msghdr* msg, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(recvmsg(fd_, msg, flags));
}

Result<uint64_t> Fd::Read(void* buf, uint64_t count) {
  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(read(fd_, buf, count));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return static_cast<uint64_t>(res);
}

Result<uint64_t> Fd::PRead(void* buf, size_t count, size_t offset) const {
  LocalErrno record_errno(const_cast<int&>(errno_));

  ssize_t res = TEMP_FAILURE_RETRY(pread(fd_, buf, count, offset));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return static_cast<uint64_t>(res);
}

#ifdef __linux__
int Fd::EventfdRead(eventfd_t* value) {
  LocalErrno record_errno(errno_);

  return eventfd_read(fd_, value);
}
#endif

ssize_t Fd::Send(const void* buf, size_t len, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(send(fd_, buf, len, flags));
}

ssize_t Fd::SendMsg(const struct msghdr* msg, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(sendmsg(fd_, msg, flags));
}

Result<uint64_t> Fd::SeekSet(uint64_t offset) {
  off_t ret = LSeek(static_cast<off_t>(offset), SEEK_SET);
  CF_EXPECT_GE(ret, 0, StrError());
  return ret;
}

Result<uint64_t> Fd::SeekCur(int64_t offset) {
  off_t ret = LSeek(static_cast<off_t>(offset), SEEK_CUR);
  CF_EXPECT_GE(ret, 0, StrError());
  return ret;
}

Result<uint64_t> Fd::SeekEnd(int64_t offset) {
  off_t ret = LSeek(static_cast<off_t>(offset), SEEK_END);
  CF_EXPECT_GE(ret, 0, StrError());
  return ret;
}

int Fd::Shutdown(int how) {
  LocalErrno record_errno(errno_);

  return shutdown(fd_, how);
}

int Fd::SetSockOpt(int level, int optname, const void* optval,
                   socklen_t optlen) {
  LocalErrno record_errno(errno_);

  return setsockopt(fd_, level, optname, optval, optlen);
}

int Fd::GetSockOpt(int level, int optname, void* optval, socklen_t* optlen) {
  LocalErrno record_errno(errno_);

  return getsockopt(fd_, level, optname, optval, optlen);
}

int Fd::SetTerminalRaw() {
  LocalErrno record_errno(errno_);

  termios terminal_settings;
  if (int rval = tcgetattr(fd_, &terminal_settings); rval < 0) {
    return rval;
  }
  cfmakeraw(&terminal_settings);
  if (int rval = tcsetattr(fd_, TCSANOW, &terminal_settings); rval < 0) {
    return rval;
  }

  // tcsetattr() succeeds if any of the requested change success.
  // So double check whether everything is applied.
  termios raw_settings;
  if (int rval = tcgetattr(fd_, &raw_settings); rval < 0) {
    return rval;
  }
  if (memcmp(&terminal_settings, &raw_settings, sizeof(terminal_settings))) {
    errno = EPROTO;
    return -1;
  }
  return 0;
}

std::string Fd::StrError() const {
  errno = 0;
  return std::string(::cuttlefish::StrError(errno_));
}

ScopedMMap Fd::MMap(void* addr, size_t length, int prot, int flags,
                    off_t offset) {
  LocalErrno record_errno(errno_);

  auto ptr = mmap(addr, length, prot, flags, fd_, offset);
  return ScopedMMap(ptr, length);
}

Result<void> Fd::Truncate(uint64_t length) {
  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(ftruncate(fd_, length));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return {};
}

Result<uint64_t> Fd::Write(const void* buf, size_t count) {
  if (count == 0 && !IsRegular()) {
    return 0;
  }

  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(write(fd_, buf, count));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return static_cast<uint64_t>(res);
}

Result<uint64_t> Fd::PWrite(const void* buf, size_t count, size_t offset) {
  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(pwrite(fd_, buf, count, offset));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return static_cast<uint64_t>(res);
}

#ifdef __linux__
int Fd::EventfdWrite(eventfd_t value) {
  LocalErrno record_errno(errno_);

  return eventfd_write(fd_, value);
}
#endif

bool Fd::IsATTY() {
  LocalErrno record_errno(errno_);

  return isatty(fd_);
}

int Fd::Futimens(const struct timespec times[2]) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(futimens(fd_, times));
}

// inotify related functions
int Fd::InotifyAddWatch(const std::string& pathname, uint32_t mask) {
  return inotify_add_watch(fd_, pathname.c_str(), mask);
}

void Fd::InotifyRmWatch(int watch) { inotify_rm_watch(fd_, watch); }

Fd::Fd(int fd, int in_errno)
    : fd_(fd), errno_(in_errno), is_regular_file_(IsRegularFile(fd_)) {
  // Ensure every file descriptor managed by a Fd has the CLOEXEC
  // flag
  TEMP_FAILURE_RETRY(fcntl(fd, F_SETFD, FD_CLOEXEC));
  std::stringstream identity;
  identity << "fd=" << fd << " @" << this;
  identity_ = identity.str();
}

}  // namespace cuttlefish
