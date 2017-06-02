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
#include "guest/gce_network/sys_client.h"

#include <stdlib.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <memory>

#include <glog/logging.h>

namespace avd {
namespace {

#ifdef __i386__
// Copied from asm-x86/asm/unistd.h or arch/sh/include/uapi/asm/unistd_32.h
// Compatible only with x86_32 CPUs.
enum SysCalls {
  kSysUnshare = 310,
  kSysSetNs = 346,
};
#elif __x86_64__
// Copied from bionic/libc/kernel/uapi/asm-x86/asm/unistd_64.h
// Compatible only with x86_64 CPUs.
enum SysCalls {
  kSysClone = 56,
  kSysUnshare = 272,
  kSysSetNs = 308,
};
#else
#error "Unsupported Architecture"
#endif

class SysClientImpl : public SysClient {
 public:
  SysClientImpl() {}
  ~SysClientImpl() {}

  // Wrapper around clone() call.
  virtual ProcessHandle* Clone(
      const std::string& name,
      const std::function<int32_t()>& call, int32_t clone_flags);

  // Wrapper around setns() call.
  virtual int32_t SetNs(int32_t fd, int32_t clone_flags);
  virtual int32_t Unshare(int32_t clone_flags);

  virtual ProcessPipe* POpen(const std::string& command);
  virtual int32_t System(const std::string& cmd);

  // Wrappers around umount() call.
  virtual int32_t Umount(const std::string& path, int32_t flags);

  // Wrappers around mount() call.
  virtual int32_t Mount(const std::string& source, const std::string& target,
                        const std::string& type, int32_t flags);

  virtual int32_t Socket(int family, int type, int proto);
  virtual int32_t IoCtl(int fd, int ioctl, void* data);
  virtual int32_t SendMsg(int fd, struct msghdr* msg, int32_t flags);
  virtual int32_t RecvMsg(int fd, struct msghdr* msg, int32_t flags);
  virtual int32_t Close(int fd);
};

class ProcessHandleImpl : public SysClient::ProcessHandle {
 public:
  ProcessHandleImpl(
      const std::string& name,
      const std::function<int32_t()>& function, int32_t flags);
  virtual ~ProcessHandleImpl();

  virtual int32_t WaitResult();
  virtual pid_t Pid();

  // Not exposed.
  bool Start();

 private:
  // This method will be invoked first by clone().
  static int CallWrapper(ProcessHandleImpl* self);

  pid_t pid_;
  std::string name_;
  const std::function<int32_t()> function_;
  int32_t clone_flags_;
};

class ProcessPipeImpl : public SysClient::ProcessPipe {
 public:
  ProcessPipeImpl(const std::string& command);
  virtual ~ProcessPipeImpl();

  virtual const char* GetOutputLine();
  virtual int32_t GetReturnCode();
  virtual bool IsCompleted();

  virtual FILE* CustomShellPOpen(const std::string& command);

 private:
  FILE* pipe_;
  std::unique_ptr<ProcessHandleImpl> handle_;

  char output_line_buffer_[512];
  int32_t return_code_;
};

ProcessHandleImpl::ProcessHandleImpl(
    const std::string& name,
    const std::function<int32_t()>& function, int32_t clone_flags)
    : pid_(0),
      name_(name),
      function_(function),
      clone_flags_(clone_flags) {}

ProcessHandleImpl::~ProcessHandleImpl() {
  if (pid_ > 0) waitpid(pid_, NULL, 0);
}

pid_t ProcessHandleImpl::Pid() {
  return pid_;
}

bool ProcessHandleImpl::Start() {
  pid_ = fork();

  if (pid_ == 0) {
    syscall(kSysUnshare, clone_flags_);
    prctl(PR_SET_NAME, name_.c_str(), 0, 0, 0);
    // Child process: run function and pass return value to caller.
    int rval = function_();
    exit(rval);
  }

  return pid_ > 0;
}

int32_t ProcessHandleImpl::WaitResult() {
  int32_t result = -1;

  if (pid_ > 0) {
    waitpid(pid_, &result, 0);
    pid_ = 0;
  }

  return result;
}

ProcessPipeImpl::ProcessPipeImpl(const std::string& command)
  : return_code_(0) {
  pipe_ = CustomShellPOpen(command + " 2>&1");
}

// We need to replace regular popen call, because android hosts its shell
// in a different directory than everyone else.
FILE* ProcessPipeImpl::CustomShellPOpen(const std::string& command) {
  int pipes[2];

  if (pipe(pipes) != 0) {
    LOG(ERROR) << "Could not create pipe: " << strerror(errno);
    return NULL;
  }

  handle_.reset(new ProcessHandleImpl(
      "exec", [pipes, command](){
        close(pipes[0]);
        dup2(pipes[1], 1);
        dup2(pipes[1], 2);
        close(pipes[1]);
        execl("/system/bin/sh", "sh", "-c", command.c_str());
        exit(0);
        return 0;
      }, SIGCHLD));
  handle_->Start();

  close(pipes[1]);
  return fdopen(pipes[0], "r");
}

ProcessPipeImpl::~ProcessPipeImpl() {
  handle_.reset();
  if (pipe_) fclose(pipe_);
}

const char* ProcessPipeImpl::GetOutputLine() {
  if (!pipe_) return NULL;

  output_line_buffer_[0] = '\0';
  return fgets(output_line_buffer_, sizeof(output_line_buffer_), pipe_);
}

int32_t ProcessPipeImpl::GetReturnCode() {
  if (pipe_) {
    return_code_ = handle_->WaitResult();
    fclose(pipe_);
    pipe_ = NULL;
  }
  return return_code_;
}

bool ProcessPipeImpl::IsCompleted() {
  if (!pipe_) return true;
  return feof(pipe_);
}

SysClient::ProcessHandle* SysClientImpl::Clone(
    const std::string& name,
    const std::function<int32_t()>& function, int32_t clone_flags) {
  ProcessHandleImpl* handle = new ProcessHandleImpl(
      name, function, clone_flags);

  if (!handle->Start()) {
    delete handle;
    return NULL;
  }

  return handle;
}

int32_t SysClientImpl::SetNs(int32_t fd, int32_t clone_flags) {
  // We provide a custom syscall here because older Android versions do not come
  // with libc wrapping the setns call,
  return syscall(kSysSetNs, fd, clone_flags);
}

int32_t SysClientImpl::Unshare(int32_t clone_flags) {
  return syscall(kSysUnshare, clone_flags);
}

SysClient::ProcessPipe* SysClientImpl::POpen(const std::string& command) {
  return new ProcessPipeImpl(command);
}

int32_t SysClientImpl::System(const std::string& command) {
  LOG(WARNING) << "*** Command " << command
               << " will likely fail to find the shell. ***";
  return system(command.c_str());
}

int32_t SysClientImpl::Umount(const std::string& path, int32_t flags) {
  return umount2(path.c_str(), flags);
}

  // Wrappers around mount() call.
int32_t SysClientImpl::Mount(
    const std::string& source, const std::string& target,
    const std::string& type, int32_t flags) {
  return mount(source.c_str(), target.c_str(), type.c_str(), flags, NULL);
}

int32_t SysClientImpl::Socket(int family, int type, int proto) {
  return socket(family, type, proto);
}

int32_t SysClientImpl::IoCtl(int fd, int iocmd, void* data) {
  return ioctl(fd, iocmd, data);
}

int32_t SysClientImpl::SendMsg(int fd, struct msghdr* msg, int32_t flags) {
  return sendmsg(fd, msg, flags);
}

int32_t SysClientImpl::RecvMsg(int fd, struct msghdr* msg, int32_t flags) {
  return recvmsg(fd, msg, flags);
}

int32_t SysClientImpl::Close(int fd) {
  return close(fd);
}

}  // namespace

// static
SysClient* SysClient::New() {
  return new SysClientImpl();
}

}  // namespace avd
