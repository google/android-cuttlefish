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
#include "cuttlefish/common/libs/fs/file_instance.h"

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

#include "cuttlefish/common/libs/fs/scoped_mmap.h"
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

bool IsRegularFile(const int fd) {
  struct stat info;
  if (fstat(fd, &info) < 0) {
    return false;
  }
  return S_ISREG(info.st_mode);
}

constexpr size_t kPreferredBufferSize = 8192;

}  // namespace

bool FileInstance::CopyFrom(FileInstance& in, size_t length,
                            FileInstance* stop) {
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

bool FileInstance::CopyAllFrom(FileInstance& in, FileInstance* stop) {
  // FileInstance may have been constructed with a non-zero errno_ value because
  // the errno variable is not zeroed out before.
  errno_ = 0;
  in.errno_ = 0;
  while (CopyFrom(in, kPreferredBufferSize, stop)) {
  }
  // Only return false if there was an actual error.
  return !GetErrno() && !in.GetErrno();
}

bool FileInstance::SendFile(FileInstance& in, off_t* offset, size_t count) {
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

void FileInstance::Close() {
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

bool FileInstance::Chmod(mode_t mode) {
  LocalErrno record_errno(errno_);

  return fchmod(fd_, mode) == 0;
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

bool FileInstance::IsSet(fd_set* in) const {
  if (IsOpen() && FD_ISSET(fd_, in)) {
    return true;
  }
  return false;
}

#if ENABLE_GCE_SHARED_FD_LOGGING
void FileInstance::Log(const char* message) { LOG(INFO) << message; }
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

/* static */ std::unique_ptr<FileInstance> FileInstance::ClosedInstance() {
  return std::unique_ptr<FileInstance>(new FileInstance(-1, EBADF));
}

int FileInstance::Bind(const struct sockaddr* addr, socklen_t addrlen) {
  LocalErrno record_errno(errno_);

  return bind(fd_, addr, addrlen);
}

int FileInstance::Connect(const struct sockaddr* addr, socklen_t addrlen) {
  LocalErrno record_errno(errno_);

  return connect(fd_, addr, addrlen);
}

int FileInstance::UNMANAGED_Dup() {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(dup(fd_));
}

int FileInstance::UNMANAGED_Dup2(int newfd) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(dup2(fd_, newfd));
}

int FileInstance::Fcntl(int command, int value) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(fcntl(fd_, command, value));
}

int FileInstance::Fsync() {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(fsync(fd_));
}

Result<void> FileInstance::Flock(int operation) {
  LocalErrno record_errno(errno_);

  CF_EXPECT(TEMP_FAILURE_RETRY(flock(fd_, operation)) == 0,
            ::cuttlefish::StrError(errno));
  return {};
}

int FileInstance::GetSockName(struct sockaddr* addr, socklen_t* addrlen) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(getsockname(fd_, addr, addrlen));
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
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(ioctl(fd_, request, val));
}

int FileInstance::LinkAtCwd(const std::string& path) {
  LocalErrno record_errno(errno_);

  std::string name = "/proc/self/fd/";
  name += std::to_string(fd_);
  return linkat(AT_FDCWD, name.c_str(), AT_FDCWD, path.c_str(),
                AT_SYMLINK_FOLLOW);
}

int FileInstance::Listen(int backlog) {
  LocalErrno record_errno(errno_);

  return listen(fd_, backlog);
}

off_t FileInstance::LSeek(off_t offset, int whence) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(lseek(fd_, offset, whence));
}

ssize_t FileInstance::Recv(void* buf, size_t len, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(recv(fd_, buf, len, flags));
}

ssize_t FileInstance::RecvMsg(struct msghdr* msg, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(recvmsg(fd_, msg, flags));
}

Result<uint64_t> FileInstance::Read(void* buf, uint64_t count) {
  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(read(fd_, buf, count));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return static_cast<uint64_t>(res);
}

Result<uint64_t> FileInstance::PRead(void* buf, size_t count,
                                     size_t offset) const {
  LocalErrno record_errno(const_cast<int&>(errno_));

  ssize_t res = TEMP_FAILURE_RETRY(pread(fd_, buf, count, offset));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return static_cast<uint64_t>(res);
}

#ifdef __linux__
int FileInstance::EventfdRead(eventfd_t* value) {
  LocalErrno record_errno(errno_);

  return eventfd_read(fd_, value);
}
#endif

ssize_t FileInstance::Send(const void* buf, size_t len, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(send(fd_, buf, len, flags));
}

ssize_t FileInstance::SendMsg(const struct msghdr* msg, int flags) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(sendmsg(fd_, msg, flags));
}

Result<uint64_t> FileInstance::SeekSet(uint64_t offset) {
  off_t ret = LSeek(static_cast<off_t>(offset), SEEK_SET);
  CF_EXPECT_GE(ret, 0, StrError());
  return ret;
}

Result<uint64_t> FileInstance::SeekCur(int64_t offset) {
  off_t ret = LSeek(static_cast<off_t>(offset), SEEK_CUR);
  CF_EXPECT_GE(ret, 0, StrError());
  return ret;
}

Result<uint64_t> FileInstance::SeekEnd(int64_t offset) {
  off_t ret = LSeek(static_cast<off_t>(offset), SEEK_END);
  CF_EXPECT_GE(ret, 0, StrError());
  return ret;
}

int FileInstance::Shutdown(int how) {
  LocalErrno record_errno(errno_);

  return shutdown(fd_, how);
}

int FileInstance::SetSockOpt(int level, int optname, const void* optval,
                             socklen_t optlen) {
  LocalErrno record_errno(errno_);

  return setsockopt(fd_, level, optname, optval, optlen);
}

int FileInstance::GetSockOpt(int level, int optname, void* optval,
                             socklen_t* optlen) {
  LocalErrno record_errno(errno_);

  return getsockopt(fd_, level, optname, optval, optlen);
}

int FileInstance::SetTerminalRaw() {
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

std::string FileInstance::StrError() const {
  errno = 0;
  return std::string(::cuttlefish::StrError(errno_));
}

ScopedMMap FileInstance::MMap(void* addr, size_t length, int prot, int flags,
                              off_t offset) {
  LocalErrno record_errno(errno_);

  auto ptr = mmap(addr, length, prot, flags, fd_, offset);
  return ScopedMMap(ptr, length);
}

Result<void> FileInstance::Truncate(uint64_t length) {
  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(ftruncate(fd_, length));
  CF_EXPECT_GE(res, 0, ::cuttlefish::StrError(errno));

  return {};
}

Result<uint64_t> FileInstance::Write(const void* buf, size_t count) {
  if (count == 0 && !IsRegular()) {
    return 0;
  }

  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(write(fd_, buf, count));
  CF_EXPECT_GE(res, 0);

  return static_cast<uint64_t>(res);
}

Result<uint64_t> FileInstance::PWrite(const void* buf, size_t count,
                                      size_t offset) {
  LocalErrno record_errno(errno_);

  ssize_t res = TEMP_FAILURE_RETRY(pwrite(fd_, buf, count, offset));
  CF_EXPECT_GE(res, 0);

  return static_cast<uint64_t>(res);
}

#ifdef __linux__
int FileInstance::EventfdWrite(eventfd_t value) {
  LocalErrno record_errno(errno_);

  return eventfd_write(fd_, value);
}
#endif

bool FileInstance::IsATTY() {
  LocalErrno record_errno(errno_);

  return isatty(fd_);
}

int FileInstance::Futimens(const struct timespec times[2]) {
  LocalErrno record_errno(errno_);

  return TEMP_FAILURE_RETRY(futimens(fd_, times));
}

// inotify related functions
int FileInstance::InotifyAddWatch(const std::string& pathname, uint32_t mask) {
  return inotify_add_watch(fd_, pathname.c_str(), mask);
}

void FileInstance::InotifyRmWatch(int watch) { inotify_rm_watch(fd_, watch); }

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
