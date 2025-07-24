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

#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"

#include <cerrno>
#include <cstring>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/subprocess.h"

namespace cuttlefish {
namespace {

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

}  // namespace

int RunWithManagedStdio(Command cmd_tmp, const std::string* stdin_str,
                        std::string* stdout_str, std::string* stderr_str,
                        SubprocessOptions options) {
  /*
   * The order of these declarations is necessary for safety. If the function
   * returns at any point, the Command will be destroyed first, closing all
   * of its references to SharedFDs. This will cause the thread internals to
   * fail their reads or writes. The ThreadJoiner then waits for the threads to
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

  auto subprocess = cmd.Start(std::move(options));
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

Result<std::string> RunAndCaptureStdout(Command command) {
  std::string standard_out;
  std::string standard_err;
  std::string command_str = command.GetShortName();
  int exit_code = RunWithManagedStdio(std::move(command), nullptr,
                                      &standard_out, &standard_err);
  LOG(DEBUG) << "Ran " << command_str << " with stdout:\n" << standard_out;
  LOG(DEBUG) << "Ran " << command_str << " with stderr:\n" << standard_err;
  CF_EXPECTF(exit_code == 0,
             "Failed to execute '{}' <args>: exit code = {}, stdout = '{}', stderr = '{}'",
             command_str, exit_code, standard_out, standard_err);
  return standard_out;
}

}  // namespace cuttlefish
