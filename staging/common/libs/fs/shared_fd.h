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

// TODO: We can't use std::shared_ptr on the older guests due to HALs.

#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_FD_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_FD_H_

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif
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

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <android-base/cmsg.h>

#ifdef __linux__
#include <linux/vm_sockets.h>
#endif

#include "common/libs/utils/result.h"

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

struct PollSharedFd;
class Epoll;
class FileInstance;
struct VhostUserVsockCid;
struct VsockCid;

/**
 * Counted reference to a FileInstance.
 *
 * This is also the place where most new FileInstances are created. The creation
 * methods correspond to the underlying POSIX calls.
 *
 * SharedFDs can be compared and stored in STL containers. The semantics are
 * slightly different from POSIX file descriptors:
 *
 * o The value of the SharedFD is the identity of its underlying FileInstance.
 *
 * o Each newly created SharedFD has a unique, closed FileInstance:
 *    SharedFD a, b;
 *    assert (a != b);
 *    a = b;
 *    assert(a == b);
 *
 * o The identity of the FileInstance is not affected by closing the file:
 *   SharedFD a, b;
 *   set<SharedFD> s;
 *   s.insert(a);
 *   assert(s.count(a) == 1);
 *   assert(s.count(b) == 0);
 *   a->Close();
 *   assert(s.count(a) == 1);
 *   assert(s.count(b) == 0);
 *
 * o FileInstances are never visibly recycled.
 *
 * o If all of the SharedFDs referring to a FileInstance go out of scope the
 *   file is closed and the FileInstance is recycled.
 *
 * Creation methods must ensure that no references to the new file descriptor
 * escape. The underlying FileInstance should have the only reference to the
 * file descriptor. Any method that needs to know the fd must be in either
 * SharedFD or FileInstance.
 *
 * SharedFDs always have an underlying FileInstance, so all of the method
 * calls are safe in accordance with the null object pattern.
 *
 * Errors on system calls that create new FileInstances, such as Open, are
 * reported with a new, closed FileInstance with the errno set.
 */
class SharedFD {
  // Give WeakFD access to the underlying shared_ptr.
  friend class WeakFD;
 public:
  inline SharedFD();
  SharedFD(const std::shared_ptr<FileInstance>& in) : value_(in) {}
  SharedFD(SharedFD const&) = default;
  SharedFD(SharedFD&& other);
  SharedFD& operator=(SharedFD const&) = default;
  SharedFD& operator=(SharedFD&& other);
  // Reference the listener as a FileInstance to make this FD type agnostic.
  static SharedFD Accept(const FileInstance& listener, struct sockaddr* addr,
                         socklen_t* addrlen);
  static SharedFD Accept(const FileInstance& listener);
  static SharedFD Dup(int unmanaged_fd);
  // All SharedFDs have the O_CLOEXEC flag after creation. To remove use the
  // Fcntl or Dup functions.
  static SharedFD Open(const char* pathname, int flags, mode_t mode = 0);
  static SharedFD Open(const std::string& pathname, int flags, mode_t mode = 0);
  static SharedFD InotifyFd();
  static SharedFD Creat(const std::string& pathname, mode_t mode);
  static int Fchdir(SharedFD);
  static Result<SharedFD> Fifo(const std::string& pathname, mode_t mode);
  static bool Pipe(SharedFD* fd0, SharedFD* fd1);
#ifdef __linux__
  static SharedFD Event(int initval = 0, int flags = 0);
#endif
  static SharedFD MemfdCreate(const std::string& name, unsigned int flags = 0);
  static SharedFD MemfdCreateWithData(const std::string& name, const std::string& data, unsigned int flags = 0);
  static SharedFD Mkstemp(std::string* path);
  static int Poll(PollSharedFd* fds, size_t num_fds, int timeout);
  static int Poll(std::vector<PollSharedFd>& fds, int timeout);
  static bool SocketPair(int domain, int type, int protocol, SharedFD* fd0,
                         SharedFD* fd1);
  static Result<std::pair<SharedFD, SharedFD>> SocketPair(int domain, int type,
                                                          int protocol);
  static SharedFD Socket(int domain, int socket_type, int protocol);
  static SharedFD SocketLocalClient(const std::string& name, bool is_abstract,
                                    int in_type);
  static SharedFD SocketLocalClient(const std::string& name, bool is_abstract,
                                    int in_type, int timeout_seconds);
  static SharedFD SocketLocalClient(int port, int type);
  static SharedFD SocketClient(const std::string& host, int port,
                               int type, std::chrono::seconds timeout = std::chrono::seconds(0));
  static SharedFD Socket6Client(const std::string& host, const std::string& interface, int port,
                                int type, std::chrono::seconds timeout = std::chrono::seconds(0));
  static SharedFD SocketLocalServer(const std::string& name, bool is_abstract,
                                    int in_type, mode_t mode);
  static SharedFD SocketLocalServer(int port, int type);

#ifdef __linux__
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
  static SharedFD VsockServer(unsigned int port, int type,
                              std::optional<int> vhost_user_vsock_listening_cid,
                              unsigned int cid = VMADDR_CID_ANY);
  static SharedFD VsockServer(
      int type, std::optional<int> vhost_user_vsock_listening_cid);
  static SharedFD VsockClient(unsigned int cid, unsigned int port, int type,
                              bool vhost_user);
#endif

  bool operator==(const SharedFD& rhs) const { return value_ == rhs.value_; }

  bool operator!=(const SharedFD& rhs) const { return value_ != rhs.value_; }

  bool operator<(const SharedFD& rhs) const { return value_ < rhs.value_; }

  bool operator<=(const SharedFD& rhs) const { return value_ <= rhs.value_; }

  bool operator>(const SharedFD& rhs) const { return value_ > rhs.value_; }

  bool operator>=(const SharedFD& rhs) const { return value_ >= rhs.value_; }

  std::shared_ptr<FileInstance> operator->() const { return value_; }

  const FileInstance& operator*() const { return *value_; }

  FileInstance& operator*() { return *value_; }

 private:
  static SharedFD ErrorFD(int error);

  std::shared_ptr<FileInstance> value_;
};

/**
 * A non-owning reference to a FileInstance. The referenced FileInstance needs
 * to be managed by a SharedFD. A WeakFD needs to be converted to a SharedFD to
 * access the underlying FileInstance.
 */
class WeakFD {
 public:
  WeakFD(SharedFD shared_fd) : value_(shared_fd.value_) {}

  // Creates a new SharedFD object that shares ownership of the underlying fd.
  // Callers need to check that the returned SharedFD is open before using it.
  SharedFD lock() const;

 private:
  std::weak_ptr<FileInstance> value_;
};

// Provides RAII semantics for memory mappings, preventing memory leaks. It does
// not however prevent use-after-free errors since the underlying pointer can be
// extracted and could survive this object.
class ScopedMMap {
 public:
  ScopedMMap();
  ScopedMMap(void* ptr, size_t size);
  ScopedMMap(const ScopedMMap& other) = delete;
  ScopedMMap& operator=(const ScopedMMap& other) = delete;
  ScopedMMap(ScopedMMap&& other);

  ~ScopedMMap();

  void* get() { return ptr_; }
  const void* get() const { return ptr_; }
  size_t len() const { return len_; }

  operator bool() const { return ptr_ != MAP_FAILED; }

  // Checks whether the interval [offset, offset + length) is contained within
  // [0, len_)
  bool WithinBounds(size_t offset, size_t length) const {
    // Don't add offset + len to avoid overflow
    return offset < len_ && len_ - offset >= length;
  }

 private:
  void* ptr_ = MAP_FAILED;
  size_t len_;
};

/**
 * Tracks the lifetime of a file descriptor and provides methods to allow
 * callers to use the file without knowledge of the underlying descriptor
 * number.
 *
 * FileInstances have two states: Open and Closed. They may start in either
 * state. However, once a FileIntance enters the Closed state it cannot be
 * reopened.
 *
 * Construction of FileInstances is limited to select classes to avoid
 * escaping file descriptors. At this point SharedFD is the only class
 * that has access. We may eventually have ScopedFD and WeakFD.
 */
class FileInstance {
  // Give SharedFD access to the aliasing constructor.
  friend class SharedFD;
  friend class Epoll;

 public:
  virtual ~FileInstance() { Close(); }

  // This can't be a singleton because our shared_ptr's aren't thread safe.
  static std::shared_ptr<FileInstance> ClosedInstance();

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
  bool CopyFrom(FileInstance& in, size_t length, FileInstance* stop = nullptr);
  // Same as CopyFrom, but reads from input until EOF is reached.
  bool CopyAllFrom(FileInstance& in, FileInstance* stop = nullptr);

  int UNMANAGED_Dup();
  int UNMANAGED_Dup2(int newfd);
  int Fchdir();
  int Fcntl(int command, int value);
  int Fsync();

  Result<void> Flock(int operation);

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
  ssize_t Read(void* buf, size_t count);
#ifdef __linux__
  int EventfdRead(eventfd_t* value);
#endif
  ssize_t Send(const void* buf, size_t len, int flags);
  ssize_t SendMsg(const struct msghdr* msg, int flags);

  template <typename... Args>
  ssize_t SendFileDescriptors(const void* buf, size_t len, Args&&... sent_fds) {
    std::vector<int> fds;
    android::base::Append(fds, std::forward<int>(sent_fds->fd_)...);
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
  ssize_t Truncate(off_t length);
  /*
   * If the file is a regular file and the count is 0, Write() may detect
   * error(s) by calling write(fd, buf, 0) declared in <unistd.h>. If detected,
   * it will return -1. If not, 0 will be returned. For non-regular files such
   * as socket or pipe, write(fd, buf, 0) is not specified. Write(), however,
   * will do nothing and just return 0.
   *
   */
  ssize_t Write(const void* buf, size_t count);
#ifdef __linux__
  int EventfdWrite(eventfd_t value);
#endif
  bool IsATTY();

  int Futimens(const struct timespec times[2]);

  // Returns the target of "/proc/getpid()/fd/" + std::to_string(fd_)
  // if appropriate
  Result<std::string> ProcFdLinkTarget() const;

  // inotify related functions
  int InotifyAddWatch(const std::string& pathname, uint32_t mask);
  void InotifyRmWatch(int watch);

 private:
  FileInstance(int fd, int in_errno);
  FileInstance* Accept(struct sockaddr* addr, socklen_t* addrlen) const;

  int fd_;
  int errno_;
  std::string identity_;
  bool is_regular_file_;
};

struct PollSharedFd {
  SharedFD fd;
  short events;
  short revents;
};

/* Methods that need both a fully defined SharedFD and a fully defined
   FileInstance. */

SharedFD::SharedFD() : value_(FileInstance::ClosedInstance()) {}

}  // namespace cuttlefish

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_FD_H_
