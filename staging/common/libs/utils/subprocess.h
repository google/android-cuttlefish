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

#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <common/libs/fs/shared_fd.h>

namespace cvd {
class Command;
class Subprocess;
class SubprocessOptions;
using SubprocessStopper = std::function<bool(Subprocess*)>;
// Kills a process by sending it the SIGKILL signal.
bool KillSubprocess(Subprocess* subprocess);

// Keeps track of a running (sub)process. Allows to wait for its completion.
// It's an error to wait twice for the same subprocess.
class Subprocess {
 public:
  enum class StdIOChannel {
    kStdIn = 0,
    kStdOut = 1,
    kStdErr = 2,
  };

  Subprocess(pid_t pid, SharedFD control, SubprocessStopper stopper = KillSubprocess)
      : pid_(pid),
        started_(pid > 0),
        control_socket_(control),
        stopper_(stopper) {}
  // The default implementation won't do because we need to reset the pid of the
  // moved object.
  Subprocess(Subprocess&&);
  ~Subprocess() = default;
  Subprocess& operator=(Subprocess&&);
  // Waits for the subprocess to complete. Returns zero if completed
  // successfully, non-zero otherwise.
  int Wait();
  // Same as waitpid(2)
  pid_t Wait(int* wstatus, int options);
  // Whether the command started successfully. It only says whether the call to
  // fork() succeeded or not, it says nothing about exec or successful
  // completion of the command, that's what Wait is for.
  bool Started() const { return started_; }
  SharedFD control_socket() { return control_socket_; }
  pid_t pid() const { return pid_; }
  bool Stop() { return stopper_(this); }

 private:
  // Copy is disabled to avoid waiting twice for the same pid (the first wait
  // frees the pid, which allows the kernel to reuse it so we may end up waiting
  // for the wrong process)
  Subprocess(const Subprocess&) = delete;
  Subprocess& operator=(const Subprocess&) = delete;
  pid_t pid_ = -1;
  bool started_ = false;
  SharedFD control_socket_;
  SubprocessStopper stopper_;
};

class SubprocessOptions {
  bool with_control_socket_;
  bool verbose_;
  bool exit_with_parent_;
  bool in_group_;
public:
  SubprocessOptions() : with_control_socket_(false), verbose_(true),
                        exit_with_parent_(true) {}

  // If with_control_socket is true the Subprocess instance will have a SharedFD
  // that enables communication with the child process.
  void WithControlSocket(bool with_control_socket) {
    with_control_socket_ = with_control_socket;
  }
  void Verbose(bool verbose) {
    verbose_ = verbose;
  }
  void ExitWithParent(bool exit_with_parent) {
    exit_with_parent_ = exit_with_parent;
  }
  // The subprocess runs as head of its own process group.
  void InGroup(bool in_group) {
    in_group_ = in_group;
  }

  bool WithControlSocket() const { return with_control_socket_; }
  bool Verbose() const { return verbose_; }
  bool ExitWithParent() const { return exit_with_parent_; }
  bool InGroup() const { return in_group_; }
};

// An executable command. Multiple subprocesses can be started from the same
// command object. This class owns any file descriptors that the subprocess
// should inherit.
class Command {
 private:
  template <typename T>
  // For every type other than SharedFD (for which there is a specialisation)
  bool BuildParameter(std::stringstream* stream, T t) {
    *stream << t;
    return true;
  }
  // Special treatment for SharedFD
  bool BuildParameter(std::stringstream* stream, SharedFD shared_fd);
  template <typename T, typename... Args>
  bool BuildParameter(std::stringstream* stream, T t, Args... args) {
    return BuildParameter(stream, t) && BuildParameter(stream, args...);
  }

 public:
  class ParameterBuilder {
   public:
    ParameterBuilder(Command* cmd) : cmd_(cmd){};
    ParameterBuilder(ParameterBuilder&& builder) = default;
    ~ParameterBuilder();

    template <typename T>
    ParameterBuilder& operator<<(T t) {
      cmd_->BuildParameter(&stream_, t);
      return *this;
    }

    void Build();

   private:
    cvd::Command* cmd_;
    std::stringstream stream_;
  };

  // Constructs a command object from the path to an executable binary and an
  // optional subprocess stopper. When not provided, stopper defaults to sending
  // SIGKILL to the subprocess.
  Command(const std::string& executable,
          SubprocessStopper stopper = KillSubprocess)
      : subprocess_stopper_(stopper) {
    command_.push_back(executable);
  }
  Command(Command&&) = default;
  // The default copy constructor is unsafe because it would mean multiple
  // closing of the inherited file descriptors. If needed it can be implemented
  // using dup(2)
  Command(const Command&) = delete;
  Command& operator=(const Command&) = delete;
  ~Command();

  // Specify the environment for the subprocesses to be started. By default
  // subprocesses inherit the parent's environment.
  void SetEnvironment(const std::vector<std::string>& env) {
    use_parent_env_ = false;
    env_ = env;
  }
  // Adds a single parameter to the command. All arguments are concatenated into
  // a single string to form a parameter. If one of those arguments is a
  // SharedFD a duplicate of it will be used and won't be closed until the
  // object is destroyed. To add multiple parameters to the command the function
  // must be called multiple times, one per parameter.
  template <typename... Args>
  bool AddParameter(Args... args) {
    std::stringstream ss;
    if (BuildParameter(&ss, args...)) {
      command_.push_back(ss.str());
      return true;
    }
    return false;
  }

  ParameterBuilder GetParameterBuilder() { return ParameterBuilder(this); }

  // Redirects the standard IO of the command.
  bool RedirectStdIO(Subprocess::StdIOChannel channel, cvd::SharedFD shared_fd);
  bool RedirectStdIO(Subprocess::StdIOChannel subprocess_channel,
                     Subprocess::StdIOChannel parent_channel);

  // Starts execution of the command. This method can be called multiple times,
  // effectively staring multiple (possibly concurrent) instances.
  Subprocess Start(SubprocessOptions options = SubprocessOptions()) const;

  std::string GetShortName() const {
    // This is safe because the constructor guarantees the name of the binary to
    // be at index 0 on the vector
    return command_[0];
  }

 private:
  std::vector<std::string> command_;
  std::map<cvd::SharedFD, int> inherited_fds_{};
  std::map<Subprocess::StdIOChannel, int> redirects_{};
  bool use_parent_env_ = true;
  std::vector<std::string> env_{};
  SubprocessStopper subprocess_stopper_;
};

/*
 * Consumes a cvd::Command and runs it, optionally managing the stdio channels.
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
int RunWithManagedStdio(cvd::Command&& command, const std::string* stdin,
                        std::string* stdout, std::string* stderr,
                        SubprocessOptions options = SubprocessOptions());

// Convenience wrapper around Command and Subprocess class, allows to easily
// execute a command and wait for it to complete. The version without the env
// parameter starts the command with the same environment as the parent. Returns
// zero if the command completed successfully, non zero otherwise.
int execute(const std::vector<std::string>& command,
            const std::vector<std::string>& env);
int execute(const std::vector<std::string>& command);

}  // namespace cvd
