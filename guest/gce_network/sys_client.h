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
#ifndef GUEST_GCE_NETWORK_SYS_CLIENT_H_
#define GUEST_GCE_NETWORK_SYS_CLIENT_H_

#include <linux/sched.h>
#include <stdint.h>
#include <sys/socket.h>

#include <functional>
#include <string>

namespace avd {

// CLONE flags, taken from linux/sched.h.
// We keep this, because Bionic does not necessarily offer these on older
// Android systems.
enum CloneFlags {
  kCloneUnspec = 0,
  kCloneVM = 0x00000100,
  kCloneFS = 0x00000200,
  kCloneFiles = 0x00000400,
  kCloneSigHand = 0x00000800,
  kClonePTrace = 0x00002000,
  kCloneVFork = 0x00004000,
  kCloneParent = 0x00008000,
  kCloneThread = 0x00010000,
  kCloneNewNS = 0x00020000,
  kCloneSysVSem = 0x00040000,
  kCloneSetTLS = 0x00080000,
  kCloneParentSetTID = 0x00100000,
  kCloneChildClearTID = 0x00200000,
  kCloneDetached = 0x00400000,
  kCloneUntraced = 0x00800000,
  kCloneChildSetTID = 0x01000000,
  kCloneNewUTS = 0x04000000,
  kCloneNewIPC = 0x08000000,
  kCloneNewUser = 0x10000000,
  kCloneNewPID = 0x20000000,
  kCloneNewNet = 0x40000000,
  kCloneIO = 0x80000000,
};

// System functions wrapper.
// Provides abstraction of system calls used by the code.
class SysClient {
 public:
  // Abstraction of Process handle class.
  class ProcessHandle {
   public:
    ProcessHandle() {}
    // Wait for process to complete and deallocate resources needed to execute
    // the process.
    virtual ~ProcessHandle() {}

    // Wait for process to complete. Return process' result.
    virtual int32_t WaitResult() = 0;

    // Acquire child process ID.
    virtual pid_t Pid() = 0;

   private:
    ProcessHandle(const ProcessHandle&);
    ProcessHandle& operator= (const ProcessHandle&);
  };

  // Abstraction of process pipe stream.
  class ProcessPipe {
   public:
    ProcessPipe() {}
    virtual ~ProcessPipe() {}

    // Read next output line from exectued command or NULL if command has
    // completed.
    virtual const char* GetOutputLine() = 0;

    // Get code returned from a command.
    // If command is still running, waits until command completes.
    virtual int32_t GetReturnCode() = 0;

    // Return information, whether the process has completed.
    virtual bool IsCompleted() = 0;

   private:
    ProcessPipe(const ProcessPipe&);
    ProcessPipe& operator= (const ProcessPipe&);
  };

  SysClient() {}
  virtual ~SysClient() {}

  // Creates new default implementation of the SysClient.
  static SysClient* New();

  // Wrapper around clone() call.
  // Caller is responsible for proper disposal of ProcessHandle* argument.
  virtual ProcessHandle* Clone(
      const std::string& name,
      const std::function<int32_t()>& call,
      int32_t clone_flags) = 0;

  // Wrapper around setns() call.
  virtual int32_t SetNs(int32_t fd, int32_t clone_flags) = 0;

  // Wrapper around unshare() call.
  virtual int32_t Unshare(int32_t clone_flags) = 0;

  // Execute command |cmd| in a new process using |popen| call.
  // Returns ProcessPipe which can be used to access new process.
  virtual ProcessPipe* POpen(const std::string& cmd) = 0;

  // Execute command |cmd| in a new process using |system| call.
  // Returns code returned by the command.
  virtual int32_t System(const std::string& cmd) = 0;

  // Unmounts specified path. |unmount_flags| specify further unmount behavior.
  virtual int32_t Umount(const std::string& path, int32_t unmount_flags) = 0;

  // Mounts |source| of type |type| at |target| location using mount flags
  // |mount_flags|.
  virtual int32_t Mount(const std::string& source, const std::string& target,
                        const std::string& type, int32_t mount_flags) = 0;

  // Create new socket.
  virtual int32_t Socket(int family, int type, int proto) = 0;

  // Execute IOCTL.
  virtual int32_t IoCtl(int fd, int ioctl, void* data) = 0;

  // Send message over socket.
  virtual int32_t SendMsg(int fd, struct msghdr* msg, int32_t flags) = 0;

  // Receive message from socket.
  virtual int32_t RecvMsg(int fd, struct msghdr* msg, int32_t flags) = 0;

  virtual int32_t Close(int fd) = 0;

 private:
  SysClient(const SysClient&);
  SysClient& operator= (const SysClient&);
};

}  // namespace avd

#endif  // GUEST_GCE_NETWORK_SYS_CLIENT_H_
