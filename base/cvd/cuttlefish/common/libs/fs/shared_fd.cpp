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
#include "cuttlefish/common/libs/fs/shared_fd.h"

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

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_select.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

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

int memfd_create_wrapper(const char* name, unsigned int flags) {
#ifdef __linux__
  return memfd_create(name, flags);
#else
  (void)flags;
  return shm_open(name, O_RDWR);
#endif
}

}  // namespace

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

SharedFD::SharedFD(SharedFD&& other) {
  value_ = std::move(other.value_);
  other.value_.reset(new FileInstance(-1, EBADF));
}

SharedFD& SharedFD::operator=(SharedFD&& other) {
  value_ = std::move(other.value_);
  other.value_.reset(new FileInstance(-1, EBADF));
  return *this;
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
  return SharedFD(
      std::shared_ptr<FileInstance>(new FileInstance(fd, error_num)));
}

bool SharedFD::Pipe(SharedFD* fd0, SharedFD* fd1) {
  int fds[2];
#ifdef __linux__
  int rval = pipe2(fds, O_CLOEXEC);
#else
  int rval = pipe(fds);
#endif
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

SharedFD SharedFD::ShmOpen(const std::string& name, int oflag, int mode) {
  errno = 0;
  int fd = shm_open(name.c_str(), oflag, mode);
  int error_num = errno;
  return std::shared_ptr<FileInstance>(new FileInstance(fd, error_num));
}
#endif

SharedFD SharedFD::MemfdCreate(const std::string& name, unsigned int flags) {
  int fd = memfd_create_wrapper(name.c_str(), flags);
  int error_num = errno;
  return std::shared_ptr<FileInstance>(new FileInstance(fd, error_num));
}

SharedFD SharedFD::MemfdCreateWithData(const std::string& name,
                                       const std::string& data,
                                       unsigned int flags) {
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

bool SharedFD::SocketPair(int domain, int type, int protocol, SharedFD* fd0,
                          SharedFD* fd1) {
  int fds[2];
  int rval = socketpair(domain, type, protocol, fds);
  if (rval != -1) {
    (*fd0) = std::shared_ptr<FileInstance>(new FileInstance(fds[0], errno));
    (*fd1) = std::shared_ptr<FileInstance>(new FileInstance(fds[1], errno));
    return true;
  }
  return false;
}

Result<std::pair<SharedFD, SharedFD>> SharedFD::SocketPair(int domain, int type,
                                                           int protocol) {
  SharedFD a, b;
  if (!SharedFD::SocketPair(domain, type, protocol, &a, &b)) {
    return CF_ERR("socketpair failed: " << ::cuttlefish::StrError(errno));
  }
  return std::make_pair(std::move(a), std::move(b));
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

SharedFD SharedFD::InotifyFd(void) {
  errno = 0;
  int fd = TEMP_FAILURE_RETRY(inotify_init1(IN_CLOEXEC));
  return SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, errno)));
}

SharedFD SharedFD::Creat(const std::string& path, mode_t mode) {
  return SharedFD::Open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int SharedFD::Fchdir(SharedFD shared_fd) {
  if (!shared_fd.value_) {
    return -1;
  }
  LocalErrno record_errno(shared_fd->errno_);

  return TEMP_FAILURE_RETRY(fchdir(shared_fd->fd_));
}

Result<SharedFD> SharedFD::Fifo(const std::string& path, mode_t mode) {
  struct stat st{};
  if (TEMP_FAILURE_RETRY(stat(path.c_str(), &st)) == 0) {
    CF_EXPECTF(TEMP_FAILURE_RETRY(remove(path.c_str())) == 0,
               "Failed to delete old file at '{}': '{}'", path,
               ::cuttlefish::StrError(errno));
  }

  CF_EXPECTF(TEMP_FAILURE_RETRY(mkfifo(path.c_str(), mode)) == 0,
             "Failed to mkfifo('{}', {:o})", path, mode);
  auto ret = Open(path, O_RDWR);
  CF_EXPECTF(ret->IsOpen(), "Failed to open '{}': '{}'", path, ret->StrError());
  return ret;
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

Result<std::pair<SharedFD, std::string>> SharedFD::Mkostemp(
    const std::string_view path, const int flags) {
  // mkostemp replaces the Xs with random selections to make a unique filename
  auto temp_path = fmt::format("{}XXXXXX", path);
  const int fd = mkostemp(temp_path.data(), flags);
  CF_EXPECTF(fd != -1, "Error creating temporary file: {}",
             ::cuttlefish::StrError(errno));
  auto shared_fd =
      SharedFD(std::shared_ptr<FileInstance>(new FileInstance(fd, 0)));
  return std::make_pair<SharedFD, std::string>(std::move(shared_fd),
                                               std::move(temp_path));
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
  if (rval->Connect(reinterpret_cast<const sockaddr*>(&addr), sizeof addr) <
      0) {
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

SharedFD SharedFD::Socket6Client(const std::string& host,
                                 const std::string& interface, int port,
                                 int type, std::chrono::seconds timeout) {
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

    if (rval->SetSockOpt(SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) ==
        -1) {
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
  if (!rval->IsOpen()) {
    return rval;
  }
  int n = 1;
  if (rval->SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
    LOG(ERROR) << "SetSockOpt failed " << rval->StrError();
    return SharedFD::ErrorFD(rval->GetErrno());
  }
  if (rval->Bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
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
  if (!abstract) {
    (void)unlink(name.c_str());
  }

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
      LOG(ERROR) << "chmod failed: " << ::cuttlefish::StrError(errno);
      // However, continue since we do have a listening socket
    }
  }
  return rval;
}

#ifdef __linux__
SharedFD SharedFD::VsockServer(
    unsigned int port, int type,
    std::optional<int> vhost_user_vsock_listening_cid, unsigned int cid) {
  if (vhost_user_vsock_listening_cid) {
    return SharedFD::SocketLocalServer(
        GetVhostUserVsockServerAddr(port, *vhost_user_vsock_listening_cid),
        false /* abstract */, type, 0666 /* mode */);
  }

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

SharedFD SharedFD::VsockServer(
    int type, std::optional<int> vhost_user_vsock_listening_cid) {
  return VsockServer(VMADDR_PORT_ANY, type, vhost_user_vsock_listening_cid);
}

std::string SharedFD::GetVhostUserVsockServerAddr(
    unsigned int port, int vhost_user_vsock_listening_cid) {
  // TODO(b/277909042): better path than /tmp/vsock_{}/vm.vsock_{}
  return fmt::format(
      "{}_{}", GetVhostUserVsockClientAddr(vhost_user_vsock_listening_cid),
      port);
}

std::string SharedFD::GetVhostUserVsockClientAddr(int cid) {
  // TODO(b/277909042): better path than /tmp/vsock_{}/vm.vsock_{}
  return fmt::format("{}/vsock_{}_{}/vm.vsock", TempDir(), cid, getuid());
}

SharedFD SharedFD::VsockClient(unsigned int cid, unsigned int port, int type,
                               bool vhost_user) {
  if (vhost_user) {
    // TODO(b/277909042): better path than /tmp/vsock_{}/vm.vsock
    auto client = SharedFD::SocketLocalClient(GetVhostUserVsockClientAddr(cid),
                                              false /* abstract */, type);
    const std::string msg = fmt::format("connect {}\n", port);
    SendAll(client, msg);

    const std::string expected_res = fmt::format("OK {}\n", port);
    std::string actual_res(expected_res.length(), ' ');
    if (ReadExact(client, &actual_res) != expected_res.length()) {
      client->Close();
      LOG(ERROR) << "cannot connect to " << cid << ":" << port;
      return client;
    }
    if (actual_res != expected_res) {
      client->Close();
      LOG(ERROR) << "response from server: " << actual_res << ", but expect "
                 << expected_res;
      return client;
    }
    return client;
  }
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

}  // namespace cuttlefish
