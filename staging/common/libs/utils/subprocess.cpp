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

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <map>
#include <set>
#include <thread>

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"

namespace {

// If a redirected-to file descriptor was already closed, it's possible that
// some inherited file descriptor duped to this file descriptor and the redirect
// would override that. This function makes sure that doesn't happen.
bool validate_redirects(
    const std::map<cuttlefish::Subprocess::StdIOChannel, int>& redirects,
    const std::map<cuttlefish::SharedFD, int>& inherited_fds) {
  // Add the redirected IO channels to a set as integers. This allows converting
  // the enum values into integers instead of the other way around.
  std::set<int> int_redirects;
  for (const auto& entry : redirects) {
    int_redirects.insert(static_cast<int>(entry.first));
  }
  for (const auto& entry : inherited_fds) {
    auto dupped_fd = entry.second;
    if (int_redirects.count(dupped_fd)) {
      LOG(ERROR) << "Requested redirect of fd(" << dupped_fd
                 << ") conflicts with inherited FD.";
      return false;
    }
  }
  return true;
}

void do_redirects(
    const std::map<cuttlefish::Subprocess::StdIOChannel, int>& redirects) {
  for (const auto& entry : redirects) {
    auto std_channel = static_cast<int>(entry.first);
    auto fd = entry.second;
    TEMP_FAILURE_RETRY(dup2(fd, std_channel));
  }
}

std::vector<const char*> ToCharPointers(const std::vector<std::string>& vect) {
  std::vector<const char*> ret = {};
  for (const auto& str : vect) {
    ret.push_back(str.c_str());
  }
  ret.push_back(NULL);
  return ret;
}
}  // namespace
namespace cuttlefish {

Subprocess::Subprocess(Subprocess&& subprocess)
    : pid_(subprocess.pid_),
      started_(subprocess.started_),
      control_socket_(subprocess.control_socket_),
      stopper_(subprocess.stopper_) {
  // Make sure the moved object no longer controls this subprocess
  subprocess.pid_ = -1;
  subprocess.started_ = false;
  subprocess.control_socket_ = SharedFD();
}

Subprocess& Subprocess::operator=(Subprocess&& other) {
  pid_ = other.pid_;
  started_ = other.started_;
  control_socket_ = other.control_socket_;
  stopper_ = other.stopper_;

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
  auto pid = pid_;  // Wait will set pid_ to -1 after waiting
  auto wait_ret = Wait(&wstatus, 0);
  if (wait_ret < 0) {
    auto error = errno;
    LOG(ERROR) << "Error on call to waitpid: " << strerror(error);
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

bool KillSubprocess(Subprocess* subprocess) {
  auto pid = subprocess->pid();
  if (pid > 0) {
    auto pgid = getpgid(pid);
    if (pgid < 0) {
      auto error = errno;
      LOG(WARNING) << "Error obtaining process group id of process with pid="
                   << pid << ": " << strerror(error);
      // Send the kill signal anyways, because pgid will be -1 it will be sent
      // to the process and not a (non-existent) group
    }
    bool is_group_head = pid == pgid;
    if (is_group_head) {
      return killpg(pid, SIGKILL) == 0;
    } else {
      return kill(pid, SIGKILL) == 0;
    }
  }
  return true;
}
Command::ParameterBuilder::~ParameterBuilder() { Build(); }
void Command::ParameterBuilder::Build() {
  auto param = stream_.str();
  stream_ = std::stringstream();
  if (param.size()) {
    cmd_->AddParameter(param);
  }
}

Command::~Command() {
  // Close all inherited file descriptors
  for (const auto& entry : inherited_fds_) {
    close(entry.second);
  }
  // Close all redirected file descriptors
  for (const auto& entry : redirects_) {
    close(entry.second);
  }
}

bool Command::BuildParameter(std::stringstream* stream, SharedFD shared_fd) {
  int fd;
  if (inherited_fds_.count(shared_fd)) {
    fd = inherited_fds_[shared_fd];
  } else {
    fd = shared_fd->Fcntl(F_DUPFD_CLOEXEC, 3);
    if (fd < 0) {
      LOG(ERROR) << "Could not acquire a new file descriptor: " << shared_fd->StrError();
      return false;
    }
    inherited_fds_[shared_fd] = fd;
  }
  *stream << fd;
  return true;
}

bool Command::RedirectStdIO(cuttlefish::Subprocess::StdIOChannel channel,
                            cuttlefish::SharedFD shared_fd) {
  if (!shared_fd->IsOpen()) {
    return false;
  }
  if (redirects_.count(channel)) {
    LOG(ERROR) << "Attempted multiple redirections of fd: "
               << static_cast<int>(channel);
    return false;
  }
  auto dup_fd = shared_fd->Fcntl(F_DUPFD_CLOEXEC, 3);
  if (dup_fd < 0) {
    LOG(ERROR) << "Could not acquire a new file descriptor: " << shared_fd->StrError();
    return false;
  }
  redirects_[channel] = dup_fd;
  return true;
}
bool Command::RedirectStdIO(Subprocess::StdIOChannel subprocess_channel,
                            Subprocess::StdIOChannel parent_channel) {
  return RedirectStdIO(subprocess_channel,
                       cuttlefish::SharedFD::Dup(static_cast<int>(parent_channel)));
}

Subprocess Command::Start(SubprocessOptions options) const {
  auto cmd = ToCharPointers(command_);
  // The parent socket will get closed on the child on the call to exec, the
  // child socket will be closed on the parent when this function returns and no
  // references to the fd are left
  SharedFD parent_socket, child_socket;
  if (options.WithControlSocket()) {
    if (!SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &parent_socket,
                              &child_socket)) {
      LOG(ERROR) << "Unable to create control socket pair: " << strerror(errno);
      return Subprocess(-1, {});
    }
    // Remove FD_CLOEXEC from the child socket, ensure the parent has it
    child_socket->Fcntl(F_SETFD, 0);
    parent_socket->Fcntl(F_SETFD, FD_CLOEXEC);
  }

  if (!validate_redirects(redirects_, inherited_fds_)) {
    return Subprocess(-1, {});
  }

  pid_t pid = fork();
  if (!pid) {
    if (options.ExitWithParent()) {
      prctl(PR_SET_PDEATHSIG, SIGHUP); // Die when parent dies
    }

    do_redirects(redirects_);
    if (options.InGroup()) {
      // This call should never fail (see SETPGID(2))
      if (setpgid(0, 0) != 0) {
        auto error = errno;
        LOG(ERROR) << "setpgid failed (" << strerror(error) << ")";
      }
    }
    for (const auto& entry : inherited_fds_) {
      if (fcntl(entry.second, F_SETFD, 0)) {
        int error_num = errno;
        LOG(ERROR) << "fcntl failed: " << strerror(error_num);
      }
    }
    int rval;
    // If use_parent_env_ is false, the current process's environment is used as
    // the environment of the child process. To force an empty emvironment for
    // the child process pass the address of a pointer to NULL
    if (use_parent_env_) {
      rval = execv(cmd[0], const_cast<char* const*>(cmd.data()));
    } else {
      auto envp = ToCharPointers(env_);
      rval = execve(cmd[0], const_cast<char* const*>(cmd.data()),
                    const_cast<char* const*>(envp.data()));
    }
    // No need for an if: if exec worked it wouldn't have returned
    LOG(ERROR) << "exec of " << cmd[0] << " failed (" << strerror(errno)
               << ")";
    exit(rval);
  }
  if (pid == -1) {
    LOG(ERROR) << "fork failed (" << strerror(errno) << ")";
  }
  if (options.Verbose()) { // "more verbose", and LOG(DEBUG) > LOG(VERBOSE)
    LOG(DEBUG) << "Started (pid: " << pid << "): " << cmd[0];
    for (int i = 1; cmd[i]; i++) {
      LOG(DEBUG) << cmd[i];
    }
  } else {
    LOG(VERBOSE) << "Started (pid: " << pid << "): " << cmd[0];
    for (int i = 1; cmd[i]; i++) {
      LOG(VERBOSE) << cmd[i];
    }
  }
  return Subprocess(pid, parent_socket, subprocess_stopper_);
}

// A class that waits for threads to exit in its destructor.
class ThreadJoiner {
std::vector<std::thread*> threads_;
public:
  ThreadJoiner(const std::vector<std::thread*> threads) : threads_(threads) {}
  ~ThreadJoiner() {
    for (auto& thread : threads_) {
      if (thread->joinable()) {
        thread->join();
      }
    }
  }
};

int RunWithManagedStdio(Command&& cmd_tmp, const std::string* stdin,
                        std::string* stdout, std::string* stderr,
                        SubprocessOptions options) {
  /*
   * The order of these declarations is necessary for safety. If the function
   * returns at any point, the cuttlefish::Command will be destroyed first, closing all
   * of its references to SharedFDs. This will cause the thread internals to fail
   * their reads or writes. The ThreadJoiner then waits for the threads to
   * complete, as running the destructor of an active std::thread crashes the
   * program.
   *
   * C++ scoping rules dictate that objects are descoped in reverse order to
   * construction, so this behavior is predictable.
   */
  std::thread stdin_thread, stdout_thread, stderr_thread;
  ThreadJoiner thread_joiner({&stdin_thread, &stdout_thread, &stderr_thread});
  cuttlefish::Command cmd = std::move(cmd_tmp);
  bool io_error = false;
  if (stdin != nullptr) {
    cuttlefish::SharedFD pipe_read, pipe_write;
    if (!cuttlefish::SharedFD::Pipe(&pipe_read, &pipe_write)) {
      LOG(ERROR) << "Could not create a pipe to write the stdin of \""
                << cmd.GetShortName() << "\"";
      return -1;
    }
    if (!cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdIn, pipe_read)) {
      LOG(ERROR) << "Could not set stdout of \"" << cmd.GetShortName()
                << "\", was already set.";
      return -1;
    }
    stdin_thread = std::thread([pipe_write, stdin, &io_error]() {
      int written = cuttlefish::WriteAll(pipe_write, *stdin);
      if (written < 0) {
        io_error = true;
        LOG(ERROR) << "Error in writing stdin to process";
      }
    });
  }
  if (stdout != nullptr) {
    cuttlefish::SharedFD pipe_read, pipe_write;
    if (!cuttlefish::SharedFD::Pipe(&pipe_read, &pipe_write)) {
      LOG(ERROR) << "Could not create a pipe to read the stdout of \""
                << cmd.GetShortName() << "\"";
      return -1;
    }
    if (!cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdOut, pipe_write)) {
      LOG(ERROR) << "Could not set stdout of \"" << cmd.GetShortName()
                << "\", was already set.";
      return -1;
    }
    stdout_thread = std::thread([pipe_read, stdout, &io_error]() {
      int read = cuttlefish::ReadAll(pipe_read, stdout);
      if (read < 0) {
        io_error = true;
        LOG(ERROR) << "Error in reading stdout from process";
      }
    });
  }
  if (stderr != nullptr) {
    cuttlefish::SharedFD pipe_read, pipe_write;
    if (!cuttlefish::SharedFD::Pipe(&pipe_read, &pipe_write)) {
      LOG(ERROR) << "Could not create a pipe to read the stderr of \""
                << cmd.GetShortName() << "\"";
      return -1;
    }
    if (!cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdErr, pipe_write)) {
      LOG(ERROR) << "Could not set stderr of \"" << cmd.GetShortName()
                << "\", was already set.";
      return -1;
    }
    stderr_thread = std::thread([pipe_read, stderr, &io_error]() {
      int read = cuttlefish::ReadAll(pipe_read, stderr);
      if (read < 0) {
        io_error = true;
        LOG(ERROR) << "Error in reading stderr from process";
      }
    });
  }

  auto subprocess = cmd.Start(options);
  if (!subprocess.Started()) {
    return -1;
  }
  auto cmd_short_name = cmd.GetShortName();
  {
    // Force the destructor to run by moving it into a smaller scope.
    // This is necessary to close the write end of the pipe.
    cuttlefish::Command forceDelete = std::move(cmd);
  }
  int wstatus;
  subprocess.Wait(&wstatus, 0);
  if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Command was interrupted by a signal: " << WTERMSIG(wstatus);
    return -1;
  }
  {
    auto join_threads = std::move(thread_joiner);
  }
  if (io_error) {
    LOG(ERROR) << "IO error communicating with " << cmd_short_name;
    return -1;
  }
  return WEXITSTATUS(wstatus);
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

}  // namespace cuttlefish
