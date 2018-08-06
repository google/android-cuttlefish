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
pid_t subprocess_impl(const char* const* command, const char* const* envp) {
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
  return pid;
}

int execute_impl(const char* const* command, const char* const* envp) {
  pid_t pid = subprocess_impl(command, envp);
  if (pid == -1) {
    return -1;
  }
  int wstatus = 0;
  waitpid(pid, &wstatus, 0);
  int retval = 0;
  if (WIFEXITED(wstatus)) {
    retval = WEXITSTATUS(wstatus);
    if (retval) {
      LOG(ERROR) << "Command " << command[0]
                 << " exited with error code: " << retval;
    }
  } else if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Command " << command[0]
               << " was interrupted by a signal: " << WTERMSIG(wstatus);
    retval = -1;
  }
  return retval;
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
pid_t subprocess(const std::vector<std::string>& command,
                 const std::vector<std::string>& env) {
  auto cmd = ToCharPointers(command);
  auto envp = ToCharPointers(env);

  return subprocess_impl(cmd.data(), &envp[0]);
}
pid_t subprocess(const std::vector<std::string>& command) {
  auto cmd = ToCharPointers(command);

  return subprocess_impl(cmd.data(), NULL);
}
int execute(const std::vector<std::string>& command,
            const std::vector<std::string>& env) {
  auto cmd = ToCharPointers(command);
  auto envp = ToCharPointers(env);

  return execute_impl(cmd.data(), &envp[0]);
}
int execute(const std::vector<std::string>& command) {
  auto cmd = ToCharPointers(command);

  return execute_impl(cmd.data(), NULL);
}
}  // namespace cvd
