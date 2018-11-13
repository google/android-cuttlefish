/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/utils/subprocess.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glog/logging.h>
namespace {

cvd::Subprocess subprocess_impl(const char* const* command,
                                const char* const* envp,
                                bool with_control_socket) {
  // The parent socket will get closed on the child on the call to exec, the
  // child socket will be closed on the parent when this function returns and no
  // references to the fd are left
  cvd::SharedFD parent_socket, child_socket;
  if (with_control_socket) {
    if (!cvd::SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &parent_socket,
                                   &child_socket)) {
      LOG(ERROR) << "Unable to create control socket pair: " << strerror(errno);
      return cvd::Subprocess(-1, {});
    }
    // Remove FD_CLOEXEC from the child socket, ensure the parent has it
    child_socket->Fcntl(F_SETFD, 0);
    parent_socket->Fcntl(F_SETFD, FD_CLOEXEC);
  }
  pid_t pid = fork();
  if (!pid) {
    int rval;
    // If envp is NULL, the current process's environment is used as the
    // environment of the child process. To force an empty emvironment for
    // the child process pass the address of a pointer to NULL
    if (envp == NULL) {
      rval = execv(command[0], const_cast<char* const*>(command));
    } else {
      rval = execve(command[0],
                    const_cast<char* const*>(command),
                    const_cast<char* const*>(envp));
    }
    // No need for an if: if exec worked it wouldn't have returned
    LOG(ERROR) << "exec of " << command[0] << " failed (" << strerror(errno)
               << ")";
    exit(rval);
  }
  if (pid == -1) {
    LOG(ERROR) << "fork failed (" << strerror(errno) << ")";
  }
  LOG(INFO) << "Started (pid: " << pid << "): " << command[0];
  int i = 1;
  while (command[i]) {
    LOG(INFO) << command[i++];
  }
  return cvd::Subprocess(pid, parent_socket);
}

std::vector<const char*> ToCharPointers(
    const std::vector<std::string>& vect) {
  std::vector<const char*> ret = {};
  for (const auto& str : vect) {
    ret.push_back(str.c_str());
  }
  ret.push_back(NULL);
  return ret;
}
}  // namespace
namespace cvd {

Subprocess::Subprocess(Subprocess&& subprocess)
    : pid_(subprocess.pid_),
      started_(subprocess.started_),
      control_socket_(subprocess.control_socket_) {
  // Make sure the moved object no longer controls this subprocess
  subprocess.pid_ = -1;
  subprocess.started_ = false;
  subprocess.control_socket_ = SharedFD();
}

Subprocess& Subprocess::operator=(Subprocess&& other) {
  pid_ = other.pid_;
  started_ = other.started_;
  control_socket_ = other.control_socket_;

  other.pid_ = -1;
  other.started_ = false;
  other.control_socket_ = SharedFD();
  return *this;
}

int Subprocess::Wait() {
  if (pid_ < 0) {
    LOG(ERROR)
        << "Attempt to wait on invalid pid(has it been waited on already?): "
        << pid_;
    return -1;
  }
  int wstatus = 0;
  auto pid = pid_; // Wait will set pid_ to -1 after waiting
  auto wait_ret = Wait(&wstatus, 0);
  if (wait_ret < 0) {
    LOG(ERROR) << "Error on call to waitpid: " << strerror(errno);
    return wait_ret;
  }
  int retval = 0;
  if (WIFEXITED(wstatus)) {
    retval = WEXITSTATUS(wstatus);
    if (retval) {
      LOG(ERROR) << "Subprocess " << pid
                 << " exited with error code: " << retval;
    }
  } else if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Subprocess " << pid
               << " was interrupted by a signal: " << WTERMSIG(wstatus);
    retval = -1;
  }
  return retval;
}
pid_t Subprocess::Wait(int* wstatus, int options) {
  if (pid_ < 0) {
    LOG(ERROR)
        << "Attempt to wait on invalid pid(has it been waited on already?): "
        << pid_;
    return -1;
  }
  auto retval = waitpid(pid_, wstatus, options);
  // We don't want to wait twice for the same process
  pid_ = -1;
  return retval;
}

Command::~Command() {
  for(const auto& entry: inherited_fds_) {
    close(entry.second);
  }
}

bool Command::BuildParameter(std::stringstream* stream, SharedFD shared_fd) {
  int fd;
  if (inherited_fds_.count(shared_fd)) {
    fd = inherited_fds_[shared_fd];
  } else {
    fd = shared_fd->UNMANAGED_Dup();
    if (fd < 0) {
      return false;
    }
    inherited_fds_[shared_fd] = fd;
  }
  *stream << fd;
  return true;
}

Subprocess Command::Start(bool with_control_socket) const {
  auto cmd = ToCharPointers(command_);
  if (use_parent_env_) {
    return subprocess_impl(cmd.data(), nullptr, with_control_socket);
  } else {
    auto envp = ToCharPointers(env_);
    return subprocess_impl(cmd.data(), envp.data(), with_control_socket);
  }
}

int execute(const std::vector<std::string>& command,
            const std::vector<std::string>& env) {
  Command cmd(command[0]);
  for (size_t i = 1; i < command.size(); ++i) {
    cmd.AddParameter(command[i]);
  }
  cmd.SetEnvironment(env);
  auto subprocess = cmd.Start();
  if (!subprocess.Started()) {
    return -1;
  }
  return subprocess.Wait();
}
int execute(const std::vector<std::string>& command) {
  Command cmd(command[0]);
  for (size_t i = 1; i < command.size(); ++i) {
    cmd.AddParameter(command[i]);
  }
  auto subprocess = cmd.Start();
  if (!subprocess.Started()) {
    return -1;
  }
  return subprocess.Wait();
}
}  // namespace cvd
