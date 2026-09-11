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

#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_FS_FILE_INSTANCE_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_FS_FILE_INSTANCE_H_

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

// Must be below sys/socket.h to support older libc
#ifdef __linux__
#include <linux/vm_sockets.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "android-base/cmsg.h"

#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

/**
 * Classes to to enable safe access to files.
 * POSIX kernels have an unfortunate habit of recycling file descriptors.
 * That can cause problems like http://b/26121457 in code that doesn't manage
 * file lifetimes properly. These classes implement an alternate interface
 * that has some advantages:
 *
 * o References to files are tightly controlled
 * o Files are auto-closed if they go out of scope
 * o Files are life-time aware. It is impossible to close the instance twice.
 * o File descriptors are always initialized. By default the descriptor is
 *   set to a closed instance.
 *
 * These classes are designed to mimic to POSIX interface as closely as
 * possible. Specifically, they don't attempt to track the type of file
 * descriptors and expose only the valid operations. This is by design, since
 * it makes it easier to convert existing code to SharedFDs and avoids the
 * possibility that new POSIX functionality will lead to large refactorings.
 */
namespace cuttlefish {
/**
 * Tracks the lifetime of a file descriptor and provides methods to allow
 * callers to use the file without knowledge of the underlying descriptor
 * number.
 *
 * Fds have two states: Open and Closed. They may start in either
 * state.
 *
 * Construction of Fds is limited to select classes to avoid
 * escaping file descriptors.
 */
class Fd : public ReaderWriterSeeker {
  // Give SharedFD access to the aliasing constructor.
  friend class SharedFD;
  friend class Fd;
  friend class Epoll;

 public:
  Fd();
  Fd(Fd&) = delete;
  Fd(Fd&&);
  ~Fd();
  Fd& operator=(Fd&) = delete;
  Fd& operator=(Fd&&);

  // All Fds have the O_CLOEXEC flag after creation. To remove use the
  // Fcntl or Dup functions.

  static Result<Fd> Accept(const Fd& listener);
  static Result<Fd> Creat(std::string_view path, mode_t mode);
  static Result<Fd> Dup(int unmanaged_fd);
  static Result<Fd> Fifo(std::string_view pathname, mode_t mode);
  static Result<Fd> MemfdCreate(std::string_view name, unsigned int flags = 0);
  static Result<std::pair<Fd, std::string>> Mkostemp(std::string_view path,
                                                     int flags = O_CLOEXEC);
  static Result<Fd> Open(std::string_view pathname, int flags, mode_t mode = 0);
  static Result<std::pair<Fd, Fd>> Pipe();
  static Result<std::pair<Fd, Fd>> SocketPair(int domain, int type,
                                              int protocol);
  static Result<Fd> Socket(int domain, int socket_type, int protocol);
  static Result<Fd> Socket6Client(
      std::string_view host, std::string_view interface, int port, int type,
      std::chrono::seconds timeout = std::chrono::seconds(0));
  static Result<Fd> SocketClient(
      std::string_view host, int port, int type,
      std::chrono::seconds timeout = std::chrono::seconds(0));
  static Result<Fd> SocketLocalClient(std::string_view name, bool is_abstract,
                                      int in_type);
  static Result<Fd> SocketLocalClient(std::string_view name, bool is_abstract,
                                      int in_type, int timeout_seconds);
  static Result<Fd> SocketLocalClient(int port, int type);
  static Result<Fd> SocketLocalServer(std::string_view name, bool is_abstract,
                                      int in_type, mode_t mode);
  static Result<Fd> SocketLocalServer(int port, int type);

#ifdef __linux__
  static Result<Fd> Event(int initval = 0, int flags = 0);
  static Result<Fd> InotifyFd();
  static Result<Fd> ShmOpen(std::string_view name, int oflag, int mode);
  // For binding in vsock, svm_cid from `cid` param would be either
  // VMADDR_CID_ANY, VMADDR_CID_LOCAL, VMADDR_CID_HOST or their own CID, and it
  // is used for indicating connections which it accepts from.
  //  * VMADDR_CID_ANY: accept from any
  //  * VMADDR_CID_LOCAL: accept from local
  //  * VMADDR_CID_HOST: accept from child vm
  //  * their own CID: accept from parent vm
  // With vhost-user-vsock, it is basically similar to VMADDR_CID_HOST, but for
  // now it has limitations that it should bind to a specific socket file which
  // is for a certain cid. So for vhost-user-vsock, we need to specify the
  // expected client's cid. That's why vhost_user_vsock_listening_cid is
  // necessary.
  // TODO: combining them when vhost-user-vsock impl supports a kind of
  // VMADDR_CID_HOST
  static std::string GetVhostUserVsockServerAddr(
      unsigned int port, int vhost_user_vsock_listening_cid);
  static std::string GetVhostUserVsockClientAddr(int cid);
  static Result<Fd> VsockServer(
      unsigned int port, int type,
      std::optional<int> vhost_user_vsock_listening_cid,
      unsigned int cid = VMADDR_CID_ANY);
  static Result<Fd> VsockServer(
      int type, std::optional<int> vhost_user_vsock_listening_cid);
#endif

  int Bind(const struct sockaddr* addr, socklen_t addrlen);
  int Connect(const struct sockaddr* addr, socklen_t addrlen);
  int ConnectWithTimeout(const struct sockaddr* addr, socklen_t addrlen,
                         struct timeval* timeout);
  void Close();

  bool Chmod(mode_t mode);

  // Returns true if the entire input was copied.
  // Otherwise an error will be set either on this file or the input.
  // The non-const reference is needed to avoid binding this to a particular
  // reference type.
  bool CopyFrom(Fd& in, size_t length, Fd* stop = nullptr);
  // Same as CopyFrom, but reads from input until EOF is reached.
  bool CopyAllFrom(Fd& in, Fd* stop = nullptr);
  bool SendFile(Fd& in, off_t* offset, size_t count);

  int UNMANAGED_Dup();
  int UNMANAGED_Dup2(int newfd);
  int Fchdir();
  int Fcntl(int command, int value);
  int Fsync();

  Result<void> Flock(int operation);

  Result<struct stat> Fstat();

  int GetErrno() const { return errno_; }
  int GetSockName(struct sockaddr* addr, socklen_t* addrlen);

#ifdef __linux__
  unsigned int VsockServerPort();
#endif

  int Ioctl(int request, void* val = nullptr);
  bool IsOpen() const { return fd_ != -1; }

  // in probably isn't modified, but the API spec doesn't have const.
  bool IsSet(fd_set* in) const;

  // whether this is a regular file or not
  bool IsRegular() const { return is_regular_file_; }

  /**
   * Adds a hard link to a file descriptor, based on the current working
   * directory of the process or to some absolute path.
   *
   * https://www.man7.org/linux/man-pages/man2/linkat.2.html
   *
   * Using this on a file opened with O_TMPFILE can link it into the filesystem.
   */
  // Used with O_TMPFILE files to attach them to the filesystem.
  int LinkAtCwd(const std::string& path);
  int Listen(int backlog);
  static void Log(const char* message);
  off_t LSeek(off_t offset, int whence);
  ssize_t Recv(void* buf, size_t len, int flags);
  ssize_t RecvMsg(struct msghdr* msg, int flags);
  Result<uint64_t> Read(void* buf, uint64_t count) override;
  Result<uint64_t> PRead(void* buf, uint64_t count,
                         uint64_t offset) const override;
#ifdef __linux__
  int EventfdRead(eventfd_t* value);
#endif
  ssize_t Send(const void* buf, size_t len, int flags);
  ssize_t SendMsg(const struct msghdr* msg, int flags);

  Result<uint64_t> SeekSet(uint64_t) override;
  Result<uint64_t> SeekCur(int64_t) override;
  Result<uint64_t> SeekEnd(int64_t) override;

  template <typename... Args>
  ssize_t SendFileDescriptors(const void* buf, size_t len, Args&&... sent_fds) {
    std::vector<int> fds;
    (fds.push_back(sent_fds->fd_), ...);
    errno = 0;
    auto ret = android::base::SendFileDescriptorVector(fd_, buf, len, fds);
    errno_ = errno;
    return ret;
  }

  int Shutdown(int how);
  void Set(fd_set* dest, int* max_index) const;
  int SetSockOpt(int level, int optname, const void* optval, socklen_t optlen);
  int GetSockOpt(int level, int optname, void* optval, socklen_t* optlen);
  int SetTerminalRaw();
  std::string StrError() const;
  ScopedMMap MMap(void* addr, size_t length, int prot, int flags, off_t offset);
  Result<void> Truncate(uint64_t length) override;
  /*
   * If the file is a regular file and the count is 0, Write() may detect
   * error(s) by calling write(fd, buf, 0) declared in <unistd.h>. If detected,
   * it will return -1. If not, 0 will be returned. For non-regular files such
   * as socket or pipe, write(fd, buf, 0) is not specified. Write(), however,
   * will do nothing and just return 0.
   *
   */
  Result<uint64_t> Write(const void* buf, uint64_t count) override;
  Result<uint64_t> PWrite(const void* buf, uint64_t count,
                          uint64_t offset) override;
#ifdef __linux__
  int EventfdWrite(eventfd_t value);
#endif
  bool IsATTY();

  int Futimens(const struct timespec times[2]);

  // inotify related functions
  int InotifyAddWatch(const std::string& pathname, uint32_t mask);
  void InotifyRmWatch(int watch);

 private:
  Fd(int fd, int in_errno);

  static Fd ErrorFD(int error);

  int fd_;
  int errno_;
  std::string identity_;
  bool is_regular_file_;
};

}  // namespace cuttlefish

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_FS_FILE_INSTANCE_H_
