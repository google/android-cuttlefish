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

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_select.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

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
  Fd::Log("select\n");
  CheckMarked(&readfds, read_set);
  CheckMarked(&writefds, write_set);
  CheckMarked(&errorfds, error_set);
  return rval;
}

SharedFD::SharedFD() : value_(std::make_shared<Fd>()) {}

SharedFD::SharedFD(SharedFD&& other) {
  value_ = std::move(other.value_);
  other.value_.reset(new Fd(-1, EBADF));
}

SharedFD::SharedFD(Fd other) {
  value_ = std::make_shared<Fd>(std::move(other));
}

SharedFD& SharedFD::operator=(SharedFD&& other) {
  value_ = std::move(other.value_);
  other.value_.reset(new Fd(-1, EBADF));
  return *this;
}

SharedFD& SharedFD::operator=(Fd other) {
  value_ = std::make_shared<Fd>(std::move(other));
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

SharedFD SharedFD::Dup(int unmanaged_fd) { return Fd::Dup(unmanaged_fd); }

bool SharedFD::Pipe(SharedFD* fd0, SharedFD* fd1) {
  int fds[2];
#ifdef __linux__
  int rval = pipe2(fds, O_CLOEXEC);
#else
  int rval = pipe(fds);
#endif
  if (rval != -1) {
    (*fd0) = std::shared_ptr<Fd>(new Fd(fds[0], errno));
    (*fd1) = std::shared_ptr<Fd>(new Fd(fds[1], errno));
    return true;
  }
  return false;
}

#ifdef __linux__
SharedFD SharedFD::Event(int initval, int flags) {
  return Fd::Event(initval, flags);
}

SharedFD SharedFD::ShmOpen(const std::string& name, int oflag, int mode) {
  return Fd::ShmOpen(name, oflag, mode);
}
#endif

SharedFD SharedFD::MemfdCreateWithData(const std::string& name,
                                       const std::string& data,
                                       unsigned int flags) {
  SharedFD memfd = Fd::MemfdCreate(name, flags);
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
  Fd unique_fd0;
  Fd unique_fd1;
  if (!Fd::SocketPair(domain, type, protocol, &unique_fd0, &unique_fd1)) {
    return false;
  }
  *fd0 = std::move(unique_fd0);
  *fd1 = std::move(unique_fd1);
  return true;
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
  return Fd::Open(path, flags, mode);
}

SharedFD SharedFD::Open(const char* path, int flags, mode_t mode) {
  return Fd::Open(path, flags, mode);
}

SharedFD SharedFD::Socket(int domain, int socket_type, int protocol) {
  return Fd::Socket(domain, socket_type, protocol);
}

Result<std::pair<SharedFD, std::string>> SharedFD::Mkostemp(
    const std::string_view path, const int flags) {
  // mkostemp replaces the Xs with random selections to make a unique filename
  auto temp_path = fmt::format("{}XXXXXX", path);
  const int fd = mkostemp(temp_path.data(), flags);
  CF_EXPECTF(fd != -1, "Error creating temporary file: {}",
             ::cuttlefish::StrError(errno));
  auto shared_fd = SharedFD(std::shared_ptr<Fd>(new Fd(fd, 0)));
  return std::make_pair<SharedFD, std::string>(std::move(shared_fd),
                                               std::move(temp_path));
}

SharedFD SharedFD::ErrorFD(int error) { return Fd::ErrorFD(error); }

SharedFD SharedFD::SocketLocalClient(const std::string& name, bool abstract,
                                     int in_type) {
  return Fd::SocketLocalClient(name, abstract, in_type);
}

SharedFD SharedFD::SocketLocalClient(const std::string& name, bool abstract,
                                     int in_type, int timeout_seconds) {
  return Fd::SocketLocalClient(name, abstract, in_type, timeout_seconds);
}

SharedFD SharedFD::SocketLocalClient(int port, int type) {
  return Fd::SocketLocalClient(port, type);
}

SharedFD SharedFD::SocketClient(const std::string& host, int port, int type,
                                std::chrono::seconds timeout) {
  return Fd::SocketClient(host, port, type, timeout);
}

SharedFD SharedFD::Socket6Client(const std::string& host,
                                 const std::string& interface, int port,
                                 int type, std::chrono::seconds timeout) {
  return Fd::Socket6Client(host, interface, port, type, timeout);
}

SharedFD SharedFD::SocketLocalServer(int port, int type) {
  return Fd::SocketLocalServer(port, type);
}

SharedFD SharedFD::SocketLocalServer(const std::string& name, bool abstract,
                                     int in_type, mode_t mode) {
  return Fd::SocketLocalServer(name, abstract, in_type, mode);
}

#ifdef __linux__
SharedFD SharedFD::VsockServer(
    unsigned int port, int type,
    std::optional<int> vhost_user_vsock_listening_cid, unsigned int cid) {
  return Fd::VsockServer(port, type, vhost_user_vsock_listening_cid, cid);
}

SharedFD SharedFD::VsockServer(
    int type, std::optional<int> vhost_user_vsock_listening_cid) {
  return VsockServer(VMADDR_PORT_ANY, type, vhost_user_vsock_listening_cid);
}

std::string SharedFD::GetVhostUserVsockServerAddr(
    unsigned int port, int vhost_user_vsock_listening_cid) {
  return Fd::GetVhostUserVsockServerAddr(port, vhost_user_vsock_listening_cid);
}

std::string SharedFD::GetVhostUserVsockClientAddr(int cid) {
  return Fd::GetVhostUserVsockClientAddr(cid);
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
