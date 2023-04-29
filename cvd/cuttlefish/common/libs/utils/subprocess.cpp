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

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"

extern char** environ;

namespace cuttlefish {
namespace {

// If a redirected-to file descriptor was already closed, it's possible that
// some inherited file descriptor duped to this file descriptor and the redirect
// would override that. This function makes sure that doesn't happen.
bool validate_redirects(
    const std::map<Subprocess::StdIOChannel, int>& redirects,
    const std::map<SharedFD, int>& inherited_fds) {
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

void do_redirects(const std::map<Subprocess::StdIOChannel, int>& redirects) {
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

std::vector<std::string> ArgsToVec(char** argv) {
  std::vector<std::string> args;
  for (int i = 0; argv && argv[i]; i++) {
    args.push_back(argv[i]);
  }
  return args;
}

std::unordered_map<std::string, std::string> EnvpToMap(char** envp) {
  std::unordered_map<std::string, std::string> env_map;
  if (!envp) {
    return env_map;
  }
  for (char** e = envp; *e != nullptr; e++) {
    std::string env_var_val(*e);
    auto tokens = android::base::Split(env_var_val, "=");
    if (tokens.size() <= 1) {
      LOG(WARNING) << "Environment var in unknown format: " << env_var_val;
      continue;
    }
    const auto var = tokens.at(0);
    tokens.erase(tokens.begin());
    env_map[var] = android::base::Join(tokens, "=");
  }
  return env_map;
}

SubprocessOptions& SubprocessOptions::Verbose(bool verbose) & {
  verbose_ = verbose;
  return *this;
}
SubprocessOptions SubprocessOptions::Verbose(bool verbose) && {
  verbose_ = verbose;
  return *this;
}

SubprocessOptions& SubprocessOptions::ExitWithParent(bool v) & {
  exit_with_parent_ = v;
  return *this;
}
SubprocessOptions SubprocessOptions::ExitWithParent(bool v) && {
  exit_with_parent_ = v;
  return *this;
}

SubprocessOptions& SubprocessOptions::InGroup(bool in_group) & {
  in_group_ = in_group;
  return *this;
}
SubprocessOptions SubprocessOptions::InGroup(bool in_group) && {
  in_group_ = in_group;
  return *this;
}

Subprocess::Subprocess(Subprocess&& subprocess)
    : pid_(subprocess.pid_),
      started_(subprocess.started_),
      stopper_(subprocess.stopper_) {
  // Make sure the moved object no longer controls this subprocess
  subprocess.pid_ = -1;
  subprocess.started_ = false;
}

Subprocess& Subprocess::operator=(Subprocess&& other) {
  pid_ = other.pid_;
  started_ = other.started_;
  stopper_ = other.stopper_;

  other.pid_ = -1;
  other.started_ = false;
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
  auto wait_ret = waitpid(pid, &wstatus, 0);
  if (wait_ret < 0) {
    auto error = errno;
    LOG(ERROR) << "Error on call to waitpid: " << strerror(error);
    return wait_ret;
  }
  int retval = 0;
  if (WIFEXITED(wstatus)) {
    retval = WEXITSTATUS(wstatus);
    if (retval) {
      LOG(DEBUG) << "Subprocess " << pid
                 << " exited with error code: " << retval;
    }
  } else if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Subprocess " << pid
               << " was interrupted by a signal: " << WTERMSIG(wstatus);
    retval = -1;
  }
  return retval;
}
int Subprocess::Wait(siginfo_t* infop, int options) {
  if (pid_ < 0) {
    LOG(ERROR)
        << "Attempt to wait on invalid pid(has it been waited on already?): "
        << pid_;
    return -1;
  }
  *infop = {};
  auto retval = waitid(P_PID, pid_, infop, options);
  // We don't want to wait twice for the same process
  bool exited = infop->si_code == CLD_EXITED || infop->si_code == CLD_DUMPED;
  bool reaped = !(options & WNOWAIT);
  if (exited && reaped) {
    pid_ = -1;
  }
  return retval;
}

StopperResult KillSubprocess(Subprocess* subprocess) {
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
    auto kill_ret = (is_group_head ? killpg : kill)(pid, SIGKILL);
    if (kill_ret == 0) {
      return StopperResult::kStopSuccess;
    }
    auto kill_cmd = is_group_head ? "killpg(" : "kill(";
    PLOG(ERROR) << kill_cmd << pid << ", SIGKILL) failed: ";
    return StopperResult::kStopFailure;
  }
  return StopperResult::kStopSuccess;
}

Command::Command(std::string executable, SubprocessStopper stopper)
    : subprocess_stopper_(stopper) {
  for (char** env = environ; *env; env++) {
    env_.emplace_back(*env);
  }
  command_.emplace_back(std::move(executable));
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

void Command::BuildParameter(std::stringstream* stream, SharedFD shared_fd) {
  int fd;
  if (inherited_fds_.count(shared_fd)) {
    fd = inherited_fds_[shared_fd];
  } else {
    fd = shared_fd->Fcntl(F_DUPFD_CLOEXEC, 3);
    CHECK(fd >= 0) << "Could not acquire a new file descriptor: "
                   << shared_fd->StrError();
    inherited_fds_[shared_fd] = fd;
  }
  *stream << fd;
}

Command& Command::RedirectStdIO(Subprocess::StdIOChannel channel,
                                SharedFD shared_fd) & {
  CHECK(shared_fd->IsOpen());
  CHECK(redirects_.count(channel) == 0)
      << "Attempted multiple redirections of fd: " << static_cast<int>(channel);
  auto dup_fd = shared_fd->Fcntl(F_DUPFD_CLOEXEC, 3);
  CHECK(dup_fd >= 0) << "Could not acquire a new file descriptor: "
                     << shared_fd->StrError();
  redirects_[channel] = dup_fd;
  return *this;
}
Command Command::RedirectStdIO(Subprocess::StdIOChannel channel,
                               SharedFD shared_fd) && {
  RedirectStdIO(channel, shared_fd);
  return std::move(*this);
}
Command& Command::RedirectStdIO(Subprocess::StdIOChannel subprocess_channel,
                                Subprocess::StdIOChannel parent_channel) & {
  return RedirectStdIO(subprocess_channel,
                       SharedFD::Dup(static_cast<int>(parent_channel)));
}
Command Command::RedirectStdIO(Subprocess::StdIOChannel subprocess_channel,
                               Subprocess::StdIOChannel parent_channel) && {
  RedirectStdIO(subprocess_channel, parent_channel);
  return std::move(*this);
}

Command& Command::SetWorkingDirectory(const std::string& path) & {
  auto fd = SharedFD::Open(path, O_RDONLY | O_PATH | O_DIRECTORY);
  CHECK(fd->IsOpen()) << "Could not open \"" << path
                      << "\" dir fd: " << fd->StrError();
  return SetWorkingDirectory(fd);
}
Command Command::SetWorkingDirectory(const std::string& path) && {
  return std::move(SetWorkingDirectory(path));
}
Command& Command::SetWorkingDirectory(SharedFD dirfd) & {
  CHECK(dirfd->IsOpen()) << "Dir fd invalid: " << dirfd->StrError();
  working_directory_ = std::move(dirfd);
  return *this;
}
Command Command::SetWorkingDirectory(SharedFD dirfd) && {
  return std::move(SetWorkingDirectory(std::move(dirfd)));
}

Subprocess Command::Start(SubprocessOptions options) const {
  auto cmd = ToCharPointers(command_);

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
    if (working_directory_->IsOpen()) {
      if (SharedFD::Fchdir(working_directory_) != 0) {
        LOG(ERROR) << "Fchdir failed: " << working_directory_->StrError();
      }
    }
    int rval;
    auto envp = ToCharPointers(env_);
    const char* executable = executable_ ? executable_->c_str() : cmd[0];
    rval = execvpe(executable, const_cast<char* const*>(cmd.data()),
                   const_cast<char* const*>(envp.data()));
    // No need for an if: if exec worked it wouldn't have returned
    LOG(ERROR) << "exec of " << cmd[0] << " with path \"" << executable
               << "\" failed (" << strerror(errno) << ")";
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
  return Subprocess(pid, subprocess_stopper_);
}

std::string Command::AsBashScript(
    const std::string& redirected_stdio_path) const {
  CHECK(inherited_fds_.empty())
      << "Bash wrapper will not have inheritied file descriptors.";
  CHECK(redirects_.empty()) << "Bash wrapper will not have redirected stdio.";

  std::string contents =
      "#!/bin/bash\n\n" + android::base::Join(command_, " \\\n");
  if (!redirected_stdio_path.empty()) {
    contents += " &> " + AbsolutePath(redirected_stdio_path);
  }
  return contents;
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

int RunWithManagedStdio(Command&& cmd_tmp, const std::string* stdin_str,
                        std::string* stdout_str, std::string* stderr_str,
                        SubprocessOptions options) {
  /*
   * The order of these declarations is necessary for safety. If the function
   * returns at any point, the Command will be destroyed first, closing all
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
  Command cmd = std::move(cmd_tmp);
  bool io_error = false;
  if (stdin_str != nullptr) {
    SharedFD pipe_read, pipe_write;
    if (!SharedFD::Pipe(&pipe_read, &pipe_write)) {
      LOG(ERROR) << "Could not create a pipe to write the stdin of \""
                << cmd.GetShortName() << "\"";
      return -1;
    }
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, pipe_read);
    stdin_thread = std::thread([pipe_write, stdin_str, &io_error]() {
      int written = WriteAll(pipe_write, *stdin_str);
      if (written < 0) {
        io_error = true;
        LOG(ERROR) << "Error in writing stdin to process";
      }
    });
  }
  if (stdout_str != nullptr) {
    SharedFD pipe_read, pipe_write;
    if (!SharedFD::Pipe(&pipe_read, &pipe_write)) {
      LOG(ERROR) << "Could not create a pipe to read the stdout of \""
                << cmd.GetShortName() << "\"";
      return -1;
    }
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, pipe_write);
    stdout_thread = std::thread([pipe_read, stdout_str, &io_error]() {
      int read = ReadAll(pipe_read, stdout_str);
      if (read < 0) {
        io_error = true;
        LOG(ERROR) << "Error in reading stdout from process";
      }
    });
  }
  if (stderr_str != nullptr) {
    SharedFD pipe_read, pipe_write;
    if (!SharedFD::Pipe(&pipe_read, &pipe_write)) {
      LOG(ERROR) << "Could not create a pipe to read the stderr of \""
                << cmd.GetShortName() << "\"";
      return -1;
    }
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, pipe_write);
    stderr_thread = std::thread([pipe_read, stderr_str, &io_error]() {
      int read = ReadAll(pipe_read, stderr_str);
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
    Command forceDelete = std::move(cmd);
  }

  int code = subprocess.Wait();
  {
    auto join_threads = std::move(thread_joiner);
  }
  if (io_error) {
    LOG(ERROR) << "IO error communicating with " << cmd_short_name;
    return -1;
  }
  return code;
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
