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

#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_FS_UNIQUE_FD_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_FS_UNIQUE_FD_H_

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
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/fs/file_instance.h"
#include "cuttlefish/result/result.h"

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
 * it makes it easier to convert existing code to UniqueFds and avoids the
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
 * UniqueFds can be compared and stored in STL containers. The semantics are
 * slightly different from POSIX file descriptors:
 *
 * o The value of the UniqueFd is the identity of its underlying FileInstance.
 *
 * o Each newly created UniqueFd has a unique, closed FileInstance:
 *    UniqueFd a, b;
 *    assert (a != b);
 *    a = b;
 *    assert(a == b);
 *
 * o FileInstances are never visibly recycled.
 *
 * o If the UniqueFd referring to a FileInstance goes out of scope the file is
 *   closed and the FileInstance is recycled.
 *
 * Creation methods must ensure that no references to the new file descriptor
 * escape. The underlying FileInstance should have the only reference to the
 * file descriptor. Any method that needs to know the fd must be in either
 * UniqueFd or FileInstance.
 *
 * UniqueFds always have an underlying FileInstance, so all of the method
 * calls are safe in accordance with the null object pattern.
 *
 * Errors on system calls that create new FileInstances, such as Open, are
 * reported with a new, closed FileInstance with the errno set.
 */
class UniqueFd {
  // Give SharedFD access to the underlying unique_ptr.
  friend class SharedFD;

 public:
  UniqueFd();
  UniqueFd(std::unique_ptr<FileInstance> in) : value_(std::move(in)) {}
  UniqueFd(UniqueFd&& other);
  UniqueFd& operator=(UniqueFd&& other);
  // Reference the listener as a FileInstance to make this FD type agnostic.
  static UniqueFd Accept(const FileInstance& listener, struct sockaddr* addr,
                         socklen_t* addrlen);
  static UniqueFd Accept(const FileInstance& listener);
  static UniqueFd Dup(int unmanaged_fd);
  // All UniqueFds have the O_CLOEXEC flag after creation. To remove use the
  // Fcntl or Dup functions.
  static UniqueFd Open(const char* pathname, int flags, mode_t mode = 0);
  static UniqueFd Open(const std::string& pathname, int flags, mode_t mode = 0);
  static UniqueFd InotifyFd();
  static UniqueFd Creat(const std::string& pathname, mode_t mode);
  static int Fchdir(UniqueFd);
  static Result<UniqueFd> Fifo(const std::string& pathname, mode_t mode);
  static bool Pipe(UniqueFd* fd0, UniqueFd* fd1);
#ifdef __linux__
  static UniqueFd Event(int initval = 0, int flags = 0);
  static UniqueFd ShmOpen(const std::string& name, int oflag, int mode);
#endif
  static UniqueFd MemfdCreate(const std::string& name, unsigned int flags = 0);
  static UniqueFd Mkstemp(std::string* path);
  static Result<std::pair<UniqueFd, std::string>> Mkostemp(
      std::string_view path, int flags = O_CLOEXEC);
  static int Poll(PollSharedFd* fds, size_t num_fds, int timeout);
  static int Poll(std::vector<PollSharedFd>& fds, int timeout);
  static bool SocketPair(int domain, int type, int protocol, UniqueFd* fd0,
                         UniqueFd* fd1);
  static Result<std::pair<UniqueFd, UniqueFd>> SocketPair(int domain, int type,
                                                          int protocol);
  static UniqueFd Socket(int domain, int socket_type, int protocol);
  static UniqueFd SocketLocalClient(const std::string& name, bool is_abstract,
                                    int in_type);
  static UniqueFd SocketLocalClient(const std::string& name, bool is_abstract,
                                    int in_type, int timeout_seconds);
  static UniqueFd SocketLocalClient(int port, int type);
  static UniqueFd SocketClient(
      const std::string& host, int port, int type,
      std::chrono::seconds timeout = std::chrono::seconds(0));
  static UniqueFd Socket6Client(
      const std::string& host, const std::string& interface, int port, int type,
      std::chrono::seconds timeout = std::chrono::seconds(0));
  static UniqueFd SocketLocalServer(const std::string& name, bool is_abstract,
                                    int in_type, mode_t mode);
  static UniqueFd SocketLocalServer(int port, int type);

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
  static UniqueFd VsockServer(unsigned int port, int type,
                              std::optional<int> vhost_user_vsock_listening_cid,
                              unsigned int cid = VMADDR_CID_ANY);
  static UniqueFd VsockServer(
      int type, std::optional<int> vhost_user_vsock_listening_cid);
  static std::string GetVhostUserVsockServerAddr(
      unsigned int port, int vhost_user_vsock_listening_cid);
  static std::string GetVhostUserVsockClientAddr(int cid);
#endif

  auto operator<=>(const UniqueFd&) const = default;

  const std::unique_ptr<FileInstance>& operator->() const { return value_; }

  const FileInstance& operator*() const { return *value_; }

  FileInstance& operator*() { return *value_; }

 private:
  static UniqueFd ErrorFD(int error);

  std::unique_ptr<FileInstance> value_;
};

}  // namespace cuttlefish

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_FS_UNIQUE_FD_H_
