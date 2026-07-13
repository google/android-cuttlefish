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
#pragma once

#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/process/subprocess.h"
#include "cuttlefish/process/subprocess_options.h"

namespace cuttlefish {

// An executable command. Multiple subprocesses can be started from the same
// command object. This class owns any file descriptors that the subprocess
// should inherit.
class Command {
 private:
  template <typename T>
  // For every type other than SharedFD (for which there is a specialisation)
  void BuildParameter(std::stringstream* stream, T t) {
    *stream << t;
  }
  // Special treatment for SharedFD
  void BuildParameter(std::stringstream* stream, SharedFD shared_fd);
  // Special treatment for bool so it uses true/false instead of 1/0
  void BuildParameter(std::stringstream* stream, bool arg);
  template <typename T, typename... Args>
  void BuildParameter(std::stringstream* stream, T t, Args... args) {
    BuildParameter(stream, t);
    BuildParameter(stream, args...);
  }

 public:
  // Constructs a command object from the path to an executable binary and an
  // optional subprocess stopper. When not provided, stopper defaults to sending
  // SIGKILL to the subprocess.
  Command(std::string executable, SubprocessStopper stopper = KillSubprocess);
  Command(Command&&) = default;
  // The default copy constructor is unsafe because it would mean multiple
  // closing of the inherited file descriptors. If needed it can be implemented
  // using dup(2)
  Command(const Command&) = delete;
  Command& operator=(const Command&) = delete;
  ~Command();

  const std::string& Executable() const {
    return executable_ ? *executable_ : command_[0];
  }

  Command& SetExecutable(std::string executable) & {
    executable_ = std::move(executable);
    return *this;
  }
  Command SetExecutable(std::string executable) && {
    return std::move(SetExecutable(executable));
  }

  Command& SetName(std::string name) & {
    command_[0] = std::move(name);
    return *this;
  }
  Command SetName(std::string name) && {
    return std::move(SetName(std::move(name)));
  }

  Command& SetExecutableAndName(std::string name) & {
    return SetExecutable(name).SetName(std::move(name));
  }

  Command SetExecutableAndName(std::string name) && {
    return std::move(SetExecutableAndName(std::move(name)));
  }

  Command& SetStopper(SubprocessStopper stopper) & {
    subprocess_stopper_ = std::move(stopper);
    return *this;
  }
  Command SetStopper(SubprocessStopper stopper) && {
    return std::move(SetStopper(std::move(stopper)));
  }

  // Specify the environment for the subprocesses to be started. By default
  // subprocesses inherit the parent's environment.
  Command& SetEnvironment(std::vector<std::string> env) & {
    env_ = std::move(env);
    return *this;
  }
  Command SetEnvironment(std::vector<std::string> env) && {
    return std::move(SetEnvironment(std::move(env)));
  }

  Command& AddEnvironmentVariable(std::string_view env_var,
                                  std::string_view value) &;
  Command AddEnvironmentVariable(std::string_view env_var,
                                 std::string_view value) &&;

  // Specify an environment variable to be unset from the parent's
  // environment for the subprocesses to be started.
  Command& UnsetFromEnvironment(std::string_view env_var) &;
  Command UnsetFromEnvironment(std::string_view env_var) &&;

  // Adds a single parameter to the command. All arguments are concatenated into
  // a single string to form a parameter. If one of those arguments is a
  // SharedFD a duplicate of it will be used and won't be closed until the
  // object is destroyed. To add multiple parameters to the command the function
  // must be called multiple times, one per parameter.
  template <typename... Args>
  Command& AddParameter(Args... args) & {
    std::stringstream ss;
    BuildParameter(&ss, args...);
    command_.push_back(ss.str());
    return *this;
  }
  template <typename... Args>
  Command AddParameter(Args... args) && {
    return std::move(AddParameter(std::forward<Args>(args)...));
  }
  Command& AddParameter(std::string arg) &;
  Command AddParameter(std::string arg) &&;

  // Similar to AddParameter, except the args are appended to the last (most
  // recently-added) parameter in the command.
  template <typename... Args>
  Command& AppendToLastParameter(Args... args) & {
    CHECK(!command_.empty()) << "There is no parameter to append to.";
    std::stringstream ss;
    BuildParameter(&ss, args...);
    command_[command_.size() - 1] += ss.str();
    return *this;
  }
  template <typename... Args>
  Command AppendToLastParameter(Args... args) && {
    return std::move(AppendToLastParameter(std::forward<Args>(args)...));
  }

  // Redirects the standard IO of the command.
  Command& RedirectStdIO(Subprocess::StdIOChannel channel,
                         SharedFD shared_fd) &;
  Command RedirectStdIO(Subprocess::StdIOChannel channel,
                        SharedFD shared_fd) &&;
  Command& RedirectStdIO(Subprocess::StdIOChannel subprocess_channel,
                         Subprocess::StdIOChannel parent_channel) &;
  Command RedirectStdIO(Subprocess::StdIOChannel subprocess_channel,
                        Subprocess::StdIOChannel parent_channel) &&;

  Command& SetWorkingDirectory(const std::string& path) &;
  Command SetWorkingDirectory(const std::string& path) &&;
  Command& SetWorkingDirectory(SharedFD dirfd) &;
  Command SetWorkingDirectory(SharedFD dirfd) &&;

  Command& AddPrerequisite(const std::function<Result<void>()>& prerequisite) &;
  Command AddPrerequisite(const std::function<Result<void>()>& prerequisite) &&;

  // Starts execution of the command. This method can be called multiple times,
  // effectively staring multiple (possibly concurrent) instances.
  Subprocess Start(SubprocessOptions options = SubprocessOptions()) const;

  std::string GetShortName() const {
    // This is safe because the constructor guarantees the name of the binary to
    // be at index 0 on the vector
    return command_[0];
  }

  std::string ToString() const;

  // Generates the contents for a bash script that can be used to run this
  // command. Note that this command must not require any file descriptors
  // or stdio redirects as those would not be available when the bash script
  // is run.
  std::string AsBashScript(const std::string& redirected_stdio_path = "") const;

 private:
  friend std::ostream& operator<<(std::ostream& out, const Command& command);
  std::optional<std::string> executable_;  // When unset, use command_[0]
  std::vector<std::string> command_;
  std::vector<std::function<Result<void>()>> prerequisites_;
  std::map<SharedFD, int> inherited_fds_{};
  std::map<Subprocess::StdIOChannel, int> redirects_{};
  std::vector<std::string> env_{};
  SubprocessStopper subprocess_stopper_;
  SharedFD working_directory_;
};
std::ostream& operator<<(std::ostream& out, const Command& command);

}  // namespace cuttlefish
