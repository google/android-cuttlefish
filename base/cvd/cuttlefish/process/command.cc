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

#include "cuttlefish/process/command.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/process/subprocess.h"
#include "cuttlefish/process/subprocess_options.h"
#include "cuttlefish/result/result.h"

#ifdef __linux__
#include <linux/prctl.h>
#include <sys/prctl.h>
#endif

extern char** environ;

namespace cuttlefish {
namespace {

// If a redirected-to file descriptor was already closed, it's possible that
// some inherited file descriptor duped to this file descriptor and the redirect
// would override that. This function makes sure that doesn't happen.
bool validate_redirects(const std::map<Command::StdIoChannel, int>& redirects,
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

void do_redirects(const std::map<Command::StdIoChannel, int>& redirects) {
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

void Command::BuildParameter(std::stringstream* stream, bool arg) {
  *stream << (arg ? "true" : "false");
}

Command& Command::AddEnvironmentVariable(std::string_view env_var,
                                         std::string_view value) & {
  env_.emplace_back(absl::StrCat(env_var, "=", value));
  return *this;
}

Command Command::AddEnvironmentVariable(std::string_view env_var,
                                        std::string_view value) && {
  AddEnvironmentVariable(env_var, value);
  return std::move(*this);
}

Command& Command::UnsetFromEnvironment(std::string_view env_var) & {
  const std::string test_value = absl::StrCat(env_var, "=");
  for (auto it = env_.begin(); it != env_.end();) {
    if (absl::StartsWith(*it, test_value)) {
      it = env_.erase(it);
    } else {
      ++it;
    }
  }
  return *this;
}

Command Command::UnsetFromEnvironment(std::string_view env_var) && {
  return std::move(UnsetFromEnvironment(env_var));
}

Command& Command::AddParameter(std::string arg) & {
  command_.emplace_back(std::move(arg));
  return *this;
}

Command Command::AddParameter(std::string arg) && {
  AddParameter(std::move(arg));
  return std::move(*this);
}

Command& Command::RedirectStdIO(Command::StdIoChannel channel,
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
Command Command::RedirectStdIO(Command::StdIoChannel channel,
                               SharedFD shared_fd) && {
  RedirectStdIO(channel, shared_fd);
  return std::move(*this);
}
Command& Command::RedirectStdIO(Command::StdIoChannel subprocess_channel,
                                Command::StdIoChannel parent_channel) & {
  return RedirectStdIO(subprocess_channel,
                       SharedFD::Dup(static_cast<int>(parent_channel)));
}
Command Command::RedirectStdIO(Command::StdIoChannel subprocess_channel,
                               Command::StdIoChannel parent_channel) && {
  RedirectStdIO(subprocess_channel, parent_channel);
  return std::move(*this);
}

Command& Command::SetWorkingDirectory(const std::string& path) & {
#ifdef __linux__
  auto fd = SharedFD::Open(path, O_RDONLY | O_PATH | O_DIRECTORY);
#elif defined(__APPLE__)
  auto fd = SharedFD::Open(path, O_RDONLY | O_DIRECTORY);
#else
#error "Unsupported operating system"
#endif
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

Command& Command::AddPrerequisite(
    const std::function<Result<void>()>& prerequisite) & {
  prerequisites_.push_back(prerequisite);
  return *this;
}

Command Command::AddPrerequisite(
    const std::function<Result<void>()>& prerequisite) && {
  prerequisites_.push_back(prerequisite);
  return std::move(*this);
}

Subprocess Command::Start(SubprocessOptions options) const {
  auto cmd = ToCharPointers(command_);

  if (!options.Strace().empty()) {
    auto strace_args = {
        "/usr/bin/strace",
        "--daemonize",
        "--output-separately",  // Add .pid suffix
        "--follow-forks",
        "-o",  // Write to a separate file.
        options.Strace().c_str(),
    };
    cmd.insert(cmd.begin(), strace_args);
  }

  if (!validate_redirects(redirects_, inherited_fds_)) {
    return Subprocess(-1, {});
  }

  for (auto& prerequisite : prerequisites_) {
    auto prerequisiteResult = prerequisite();

    if (!prerequisiteResult.has_value()) {
      LOG(ERROR) << "Failed to check prerequisites: "
                 << prerequisiteResult.error();
      return Subprocess(-1, {});
    }
  }

  // ToCharPointers allocates memory so it can't be called in the child process.
  auto envp = ToCharPointers(env_);
  pid_t pid = fork();
  if (!pid) {
    // LOG(...) can't be used in the child process because it may block waiting
    // for other threads which don't exist in the child process.
#ifdef __linux__
    if (options.ExitWithParent()) {
      prctl(PR_SET_PDEATHSIG, SIGHUP);  // Die when parent dies
    }
#endif

    do_redirects(redirects_);

    if (options.InGroup()) {
      // This call should never fail (see SETPGID(2))
      if (setpgid(0, 0) != 0) {
        exit(-errno);
      }
    }
    for (const auto& entry : inherited_fds_) {
      if (fcntl(entry.second, F_SETFD, 0)) {
        exit(-errno);
      }
    }
    if (working_directory_->IsOpen()) {
      if (SharedFD::Fchdir(working_directory_) != 0) {
        exit(-errno);
      }
    }
    int rval;
    const char* executable = executable_ ? executable_->c_str() : cmd[0];
#ifdef __linux__
    rval = execvpe(executable, const_cast<char* const*>(cmd.data()),
                   const_cast<char* const*>(envp.data()));
#elif defined(__APPLE__)
    rval = execve(executable, const_cast<char* const*>(cmd.data()),
                  const_cast<char* const*>(envp.data()));
#else
#error "Unsupported architecture"
#endif
    // No need to check for error, execvpe/execve don't return on success.
    exit(rval);
  }
  if (pid == -1) {
    LOG(ERROR) << "fork failed (" << strerror(errno) << ")";
  }
  if (options.Verbose()) {  // "more verbose", and VLOG(0) > VLOG(1)
    VLOG(0) << "Started (pid: " << pid << "): " << cmd[0];
    for (int i = 1; cmd[i]; i++) {
      VLOG(0) << cmd[i];
    }
  } else {
    VLOG(1) << "Started (pid: " << pid << "): " << cmd[0];
    for (int i = 1; cmd[i]; i++) {
      VLOG(1) << cmd[i];
    }
  }
  return Subprocess(pid, subprocess_stopper_);
}

std::ostream& operator<<(std::ostream& out, const Command& command) {
  std::unordered_set<std::string> to_show{"HOME",
                                          "ANDROID_HOST_OUT",
                                          "ANDROID_SOONG_HOST_OUT",
                                          "ANDROID_PRODUCT_OUT",
                                          "CUTTLEFISH_CONFIG_FILE",
                                          "CUTTLEFISH_INSTANCE"};
  for (const std::string& env_var : command.env_) {
    std::vector<std::string_view> env_split =
        absl::StrSplit(env_var, absl::MaxSplits('=', 1));
    if (!env_split.empty() && Contains(to_show, env_split.front())) {
      out << env_var << " ";
    }
  }
  return out << absl::StrJoin(command.command_, " ");
}

std::string Command::ToString() const {
  std::vector<std::string_view> elements;
  elements.reserve(command_.size() + env_.size());
  elements.insert(elements.end(), env_.begin(), env_.end());
  elements.insert(elements.end(), command_.begin(), command_.end());
  return absl::StrJoin(elements, " ");
}

std::string Command::AsBashScript(
    const std::string& redirected_stdio_path) const {
  CHECK(inherited_fds_.empty())
      << "Bash wrapper will not have inheritied file descriptors.";
  CHECK(redirects_.empty()) << "Bash wrapper will not have redirected stdio.";

  std::string contents =
      "#!/usr/bin/env bash\n\n" + absl::StrJoin(command_, " \\\n");
  if (!redirected_stdio_path.empty()) {
    contents += " &> " + AbsolutePath(redirected_stdio_path);
  }
  return contents;
}

}  // namespace cuttlefish
