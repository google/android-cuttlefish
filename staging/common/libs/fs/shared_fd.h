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

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <memory>
#include <sstream>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "vm_sockets.h"

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
namespace cvd {

class FileInstance;

/**
 * Counted reference to a FileInstance.
 *
 * This is also the place where most new FileInstances are created. The creation
 * mehtods correspond to the underlying POSIX calls.
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
 *    asssert(a == b);
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
 public:
  inline SharedFD();
  SharedFD(const std::shared_ptr<FileInstance>& in) : value_(in) {}
  // Reference the listener as a FileInstance to make this FD type agnostic.
  static SharedFD Accept(const FileInstance& listener, struct sockaddr* addr,
                         socklen_t* addrlen);
  static SharedFD Accept(const FileInstance& listener);
  static SharedFD Dup(int unmanaged_fd);
  // All SharedFDs have the O_CLOEXEC flag after creation. To remove use the
  // Fcntl or Dup functions.
  static SharedFD Open(const char* pathname, int flags, mode_t mode = 0);
  static SharedFD Open(const std::string& pathname, int flags, mode_t mode = 0) {
    return Open(pathname.c_str(), flags, mode);
  }
  static SharedFD Creat(const char* pathname, mode_t mode);
  static SharedFD Creat(const std::string& pathname, mode_t mode) {
    return Creat(pathname.c_str(), mode);
  }
  static bool Pipe(SharedFD* fd0, SharedFD* fd1);
  static SharedFD Event(int initval = 0, int flags = 0);
  static SharedFD MemfdCreate(const char* name, unsigned int flags = 0);
  static SharedFD MemfdCreate(const std::string& name, unsigned int flags = 0) {
    return MemfdCreate(name.c_str(), flags);
  }
  static bool SocketPair(int domain, int type, int protocol, SharedFD* fd0,
                         SharedFD* fd1);
  static SharedFD Socket(int domain, int socket_type, int protocol);
  static SharedFD SocketLocalClient(const char* name, bool is_abstract,
                                    int in_type);
  static SharedFD SocketLocalClient(const std::string& name, bool is_abstract,
                                    int in_type) {
    return SocketLocalClient(name.c_str(), is_abstract, in_type);
  }
  static SharedFD SocketLocalClient(int port, int type);
  static SharedFD SocketLocalServer(const char* name, bool is_abstract,
                                    int in_type, mode_t mode);
  static SharedFD SocketLocalServer(const std::string& name, bool is_abstract,
                                    int in_type, mode_t mode) {
    return SocketLocalServer(name.c_str(), is_abstract, in_type, mode);
  }
  static SharedFD SocketLocalServer(int port, int type);
  static SharedFD VsockServer(unsigned int port, int type);
  static SharedFD VsockServer(int type);
  static SharedFD VsockClient(unsigned int cid, unsigned int port, int type);

  bool operator==(const SharedFD& rhs) const { return value_ == rhs.value_; }

  bool operator!=(const SharedFD& rhs) const { return value_ != rhs.value_; }

  bool operator<(const SharedFD& rhs) const { return value_ < rhs.value_; }

  bool operator<=(const SharedFD& rhs) const { return value_ <= rhs.value_; }

  bool operator>(const SharedFD& rhs) const { return value_ > rhs.value_; }

  bool operator>=(const SharedFD& rhs) const { return value_ >= rhs.value_; }

  std::shared_ptr<FileInstance> operator->() const { return value_; }

  const cvd::FileInstance& operator*() const { return *value_; }

  cvd::FileInstance& operator*() { return *value_; }

 private:
  static SharedFD ErrorFD(int error);

  std::shared_ptr<FileInstance> value_;
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

 public:
  virtual ~FileInstance() { Close(); }

  // This can't be a singleton because our shared_ptr's aren't thread safe.
  static std::shared_ptr<FileInstance> ClosedInstance() {
    return std::shared_ptr<FileInstance>(new FileInstance(-1, EBADF));
  }

  int Bind(const struct sockaddr* addr, socklen_t addrlen) {
    errno = 0;
    int rval = bind(fd_, addr, addrlen);
    errno_ = errno;
    return rval;
  }

  int Connect(const struct sockaddr* addr, socklen_t addrlen) {
    errno = 0;
    int rval = connect(fd_, addr, addrlen);
    errno_ = errno;
    return rval;
  }

  void Close();

  // Returns true if the entire input was copied.
  // Otherwise an error will be set either on this file or the input.
  // The non-const reference is needed to avoid binding this to a particular
  // reference type.
  bool CopyFrom(FileInstance& in, size_t length);

  int UNMANAGED_Dup() {
    errno = 0;
    int rval = TEMP_FAILURE_RETRY(dup(fd_));
    errno_ = errno;
    return rval;
  }

  int UNMANAGED_Dup2(int newfd) {
    errno = 0;
    int rval = TEMP_FAILURE_RETRY(dup2(fd_, newfd));
    errno_ = errno;
    return rval;
  }

  int Fcntl(int command, int value) {
    errno = 0;
    int rval = TEMP_FAILURE_RETRY(fcntl(fd_, command, value));
    errno_ = errno;
    return rval;
  }

  int GetErrno() const { return errno_; }

  int GetSockName(struct sockaddr* addr, socklen_t* addrlen) {
    errno = 0;
    int rval = TEMP_FAILURE_RETRY(getsockname(fd_, addr, addrlen));
    if (rval == -1) {
      errno_ = errno;
    }
    return rval;
  }

  unsigned int VsockServerPort() {
    struct sockaddr_vm vm_socket;
    socklen_t length = sizeof(vm_socket);
    GetSockName(reinterpret_cast<struct sockaddr*>(&vm_socket), &length);
    return vm_socket.svm_port;
  }

  int Ioctl(int request, void* val = nullptr) {
    errno = 0;
    int rval = TEMP_FAILURE_RETRY(ioctl(fd_, request, val));
    errno_ = errno;
    return rval;
  }

  bool IsOpen() const { return fd_ != -1; }

  // in probably isn't modified, but the API spec doesn't have const.
  bool IsSet(fd_set* in) const;

  int Listen(int backlog) {
    errno = 0;
    int rval = listen(fd_, backlog);
    errno_ = errno;
    return rval;
  }

  static void Log(const char* message);

  off_t LSeek(off_t offset, int whence) {
    errno = 0;
    off_t rval = TEMP_FAILURE_RETRY(lseek(fd_, offset, whence));
    errno_ = errno;
    return rval;
  }

  ssize_t Recv(void* buf, size_t len, int flags) {
    errno = 0;
    ssize_t rval = TEMP_FAILURE_RETRY(recv(fd_, buf, len, flags));
    errno_ = errno;
    return rval;
  }

  ssize_t RecvMsg(struct msghdr* msg, int flags) {
    errno = 0;
    ssize_t rval = TEMP_FAILURE_RETRY(recvmsg(fd_, msg, flags));
    errno_ = errno;
    return rval;
  }

  ssize_t Read(void* buf, size_t count) {
    errno = 0;
    ssize_t rval = TEMP_FAILURE_RETRY(read(fd_, buf, count));
    errno_ = errno;
    return rval;
  }

  ssize_t Send(const void* buf, size_t len, int flags) {
    errno = 0;
    ssize_t rval = TEMP_FAILURE_RETRY(send(fd_, buf, len, flags));
    errno_ = errno;
    return rval;
  }

  ssize_t SendMsg(const struct msghdr* msg, int flags) {
    errno = 0;
    ssize_t rval = TEMP_FAILURE_RETRY(sendmsg(fd_, msg, flags));
    errno_ = errno;
    return rval;
  }

  int Shutdown(int how) {
    errno = 0;
    int rval = shutdown(fd_, how);
    errno_ = errno;
    return rval;
  }

  void Set(fd_set* dest, int* max_index) const;

  int SetSockOpt(int level, int optname, const void* optval, socklen_t optlen) {
    errno = 0;
    int rval = setsockopt(fd_, level, optname, optval, optlen);
    errno_ = errno;
    return rval;
  }

  const char* StrError() const {
    errno = 0;
    FileInstance* s = const_cast<FileInstance*>(this);
    char* out = strerror_r(errno_, s->strerror_buf_, sizeof(strerror_buf_));

    // From man page:
    //  strerror_r() returns a pointer to a string containing the error message.
    //  This may be either a pointer to a string that the function stores in
    //  buf, or a pointer to some (immutable) static string (in which case buf
    //  is unused).
    if (out != s->strerror_buf_) {
      strncpy(s->strerror_buf_, out, sizeof(strerror_buf_));
    }
    return strerror_buf_;
  }

  ssize_t Truncate(off_t length) {
    errno = 0;
    ssize_t rval = TEMP_FAILURE_RETRY(ftruncate(fd_, length));
    errno_ = errno;
    return rval;
  }

  ssize_t Write(const void* buf, size_t count) {
    errno = 0;
    ssize_t rval = TEMP_FAILURE_RETRY(write(fd_, buf, count));
    errno_ = errno;
    return rval;
  }

 private:
  FileInstance(int fd, int in_errno) : fd_(fd), errno_(in_errno) {
    // Ensure every file descriptor managed by a FileInstance has the CLOEXEC
    // flag
    TEMP_FAILURE_RETRY(fcntl(fd, F_SETFD, FD_CLOEXEC));
    std::stringstream identity;
    identity << "fd=" << fd << " @" << this;
    identity_ = identity.str();
  }

  FileInstance* Accept(struct sockaddr* addr, socklen_t* addrlen) const {
    int fd = TEMP_FAILURE_RETRY(accept(fd_, addr, addrlen));
    if (fd == -1) {
      return new FileInstance(fd, errno);
    } else {
      return new FileInstance(fd, 0);
    }
  }

  int fd_;
  int errno_;
  std::string identity_;
  char strerror_buf_[160];
};

/* Methods that need both a fully defined SharedFD and a fully defined
   FileInstance. */

inline SharedFD::SharedFD() : value_(FileInstance::ClosedInstance()) {}

}  // namespace cvd

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_FD_H_
