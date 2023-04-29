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

#include <sys/types.h>
#include <sys/wait.h>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

/*
 * Does what ArgsToVec(int argc, char**) from flag_parser.h does
 * without argc.
 */
std::vector<std::string> ArgsToVec(char** argv);
std::unordered_map<std::string, std::string> EnvpToMap(char** envp);

enum class StopperResult {
  kStopFailure, /* Failed to stop the subprocess. */
  kStopCrash,   /* Attempted to stop the subprocess cleanly, but that failed. */
  kStopSuccess, /* The subprocess exited in the expected way. */
};

class Subprocess;
using SubprocessStopper = std::function<StopperResult(Subprocess*)>;
// Kills a process by sending it the SIGKILL signal.
StopperResult KillSubprocess(Subprocess* subprocess);

// Keeps track of a running (sub)process. Allows to wait for its completion.
// It's an error to wait twice for the same subprocess.
class Subprocess {
 public:
  enum class StdIOChannel {
    kStdIn = 0,
    kStdOut = 1,
    kStdErr = 2,
  };

  Subprocess(pid_t pid, SubprocessStopper stopper = KillSubprocess)
      : pid_(pid),
        started_(pid > 0),
        stopper_(stopper) {}
  // The default implementation won't do because we need to reset the pid of the
  // moved object.
  Subprocess(Subprocess&&);
  ~Subprocess() = default;
  Subprocess& operator=(Subprocess&&);
  // Waits for the subprocess to complete. Returns zero if completed
  // successfully, non-zero otherwise.
  int Wait();
  // Same as waitid(2)
  int Wait(siginfo_t* infop, int options);
  // Whether the command started successfully. It only says whether the call to
  // fork() succeeded or not, it says nothing about exec or successful
  // completion of the command, that's what Wait is for.
  bool Started() const { return started_; }
  pid_t pid() const { return pid_; }
  StopperResult Stop() { return stopper_(this); }

 private:
  // Copy is disabled to avoid waiting twice for the same pid (the first wait
  // frees the pid, which allows the kernel to reuse it so we may end up waiting
  // for the wrong process)
  Subprocess(const Subprocess&) = delete;
  Subprocess& operator=(const Subprocess&) = delete;
  pid_t pid_ = -1;
  bool started_ = false;
  SubprocessStopper stopper_;
};

class SubprocessOptions {
 public:
  SubprocessOptions()
      : verbose_(true), exit_with_parent_(true), in_group_(false) {}

  SubprocessOptions& Verbose(bool verbose) &;
  SubprocessOptions Verbose(bool verbose) &&;
  SubprocessOptions& ExitWithParent(bool exit_with_parent) &;
  SubprocessOptions ExitWithParent(bool exit_with_parent) &&;
  // The subprocess runs as head of its own process group.
  SubprocessOptions& InGroup(bool in_group) &;
  SubprocessOptions InGroup(bool in_group) &&;

  bool Verbose() const { return verbose_; }
  bool ExitWithParent() const { return exit_with_parent_; }
  bool InGroup() const { return in_group_; }

 private:
  bool verbose_;
  bool exit_with_parent_;
  bool in_group_;
};

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

  Command& AddEnvironmentVariable(const std::string& env_var,
                                  const std::string& value) & {
    return AddEnvironmentVariable(env_var + "=" + value);
  }
  Command AddEnvironmentVariable(const std::string& env_var,
                                 const std::string& value) && {
    AddEnvironmentVariable(env_var, value);
    return std::move(*this);
  }

  Command& AddEnvironmentVariable(std::string env_var) & {
    env_.emplace_back(std::move(env_var));
    return *this;
  }
  Command AddEnvironmentVariable(std::string env_var) && {
    return std::move(AddEnvironmentVariable(std::move(env_var)));
  }

  // Specify an environment variable to be unset from the parent's
  // environment for the subprocesses to be started.
  Command& UnsetFromEnvironment(const std::string& env_var) & {
    auto it = env_.begin();
    while (it != env_.end()) {
      if (android::base::StartsWith(*it, env_var + "=")) {
        it = env_.erase(it);
      } else {
        ++it;
      }
    }
    return *this;
  }
  Command UnsetFromEnvironment(const std::string& env_var) && {
    return std::move(UnsetFromEnvironment(env_var));
  }

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

  // Starts execution of the command. This method can be called multiple times,
  // effectively staring multiple (possibly concurrent) instances.
  Subprocess Start(SubprocessOptions options = SubprocessOptions()) const;

  std::string GetShortName() const {
    // This is safe because the constructor guarantees the name of the binary to
    // be at index 0 on the vector
    return command_[0];
  }

  // Generates the contents for a bash script that can be used to run this
  // command. Note that this command must not require any file descriptors
  // or stdio redirects as those would not be available when the bash script
  // is run.
  std::string AsBashScript(const std::string& redirected_stdio_path = "") const;

 private:
  std::optional<std::string> executable_;  // When unset, use command_[0]
  std::vector<std::string> command_;
  std::map<SharedFD, int> inherited_fds_{};
  std::map<Subprocess::StdIOChannel, int> redirects_{};
  std::vector<std::string> env_{};
  SubprocessStopper subprocess_stopper_;
  SharedFD working_directory_;
};

/*
 * Consumes a Command and runs it, optionally managing the stdio channels.
 *
 * If `stdin` is set, the subprocess stdin will be pipe providing its contents.
 * If `stdout` is set, the subprocess stdout will be captured and saved to it.
 * If `stderr` is set, the subprocess stderr will be captured and saved to it.
 *
 * If `command` exits normally, the lower 8 bits of the return code will be
 * returned in a value between 0 and 255.
 * If some setup fails, `command` fails to start, or `command` exits due to a
 * signal, the return value will be negative.
 */
int RunWithManagedStdio(Command&& command, const std::string* stdin,
                        std::string* stdout, std::string* stderr,
                        SubprocessOptions options = SubprocessOptions());

// Convenience wrapper around Command and Subprocess class, allows to easily
// execute a command and wait for it to complete. The version without the env
// parameter starts the command with the same environment as the parent. Returns
// zero if the command completed successfully, non zero otherwise.
int execute(const std::vector<std::string>& command,
            const std::vector<std::string>& env);
int execute(const std::vector<std::string>& command);

}  // namespace cuttlefish
