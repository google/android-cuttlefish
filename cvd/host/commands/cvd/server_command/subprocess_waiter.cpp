/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "common/libs/fs/shared_buf.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"

namespace cuttlefish {

Result<void> SubprocessWaiter::Setup(Subprocess subprocess) {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(!subprocess_, "Already running");

  subprocess_ = std::move(subprocess);
  return {};
}

Result<siginfo_t> SubprocessWaiter::Wait() {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(subprocess_.has_value());

  siginfo_t infop{};

  interrupt_lock.unlock();

  // This blocks until the process exits, but doesn't reap it.
  auto result = subprocess_->Wait(&infop, WEXITED | WNOWAIT);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  interrupt_lock.lock();
  // Perform a reaping wait on the process (which should already have exited).
  result = subprocess_->Wait(&infop, WEXITED);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  // The double wait avoids a race around the kernel reusing pids. Waiting
  // with WNOWAIT won't cause the child process to be reaped, so the kernel
  // won't reuse the pid until the Wait call below, and any kill signals won't
  // reach unexpected processes.

  subprocess_ = {};

  return infop;
}

Result<void> SubprocessWaiter::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  if (subprocess_) {
    auto stop_result = subprocess_->Stop();
    switch (stop_result) {
      case StopperResult::kStopFailure:
        return CF_ERR("Failed to stop subprocess");
      case StopperResult::kStopCrash:
        return CF_ERR("Stopper caused process to crash");
      case StopperResult::kStopSuccess:
        return {};
      default:
        return CF_ERR("Unknown stop result: " << (uint64_t)stop_result);
    }
  }
  return {};
}

Result<RunOutput> SubprocessWaiter::RunWithManagedStdioInterruptable(
    RunWithManagedIoParam& param) {
  RunOutput output;
  std::thread stdin_thread, stdout_thread, stderr_thread;
  std::vector<std::thread*> threads_({&stdin_thread, &stdout_thread, &stderr_thread});
  Command cmd = std::move(param.cmd_);
  bool io_error = false;
  if (param.stdin_) {
    SharedFD pipe_read, pipe_write;
    CF_EXPECT(SharedFD::Pipe(&pipe_read, &pipe_write),
              "Could not create a pipe to write the stdin of \""
              << cmd.GetShortName() << "\"");

    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, pipe_read);
    stdin_thread = std::thread([pipe_write, &param, &io_error]() {
      int written = WriteAll(pipe_write, *param.stdin_);
      if (written < 0) {
        io_error = true;
        LOG(ERROR) << "Error in writing stdin to process";
      }
    });
  }
  if (param.redirect_stdout_) {
    SharedFD pipe_read, pipe_write;
    CF_EXPECT(SharedFD::Pipe(&pipe_read, &pipe_write),
              "Could not create a pipe to read the stdout of \""
              << cmd.GetShortName() << "\"");

    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, pipe_write);
    stdout_thread = std::thread([pipe_read, &output, &io_error]() {
      int read = ReadAll(pipe_read, &output.stdout_);
      if (read < 0) {
        io_error = true;
        LOG(ERROR) << "Error in reading stdout from process";
      }
    });
  }
  if (param.redirect_stderr_ == true) {
    SharedFD pipe_read, pipe_write;
    CF_EXPECT(SharedFD::Pipe(&pipe_read, &pipe_write),
              "Could not create a pipe to read the stderr of \""
                << cmd.GetShortName() << "\"");

    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, pipe_write);
    stderr_thread = std::thread([pipe_read, &output, &io_error]() {
      int read = ReadAll(pipe_read, &output.stderr_);
      if (read < 0) {
        io_error = true;
        LOG(ERROR) << "Error in reading stderr from process";
      }
    });
  }

  // lower half
  auto subprocess = cmd.Start(param.options_);
  CF_EXPECT(subprocess.Started());

  auto cmd_short_name = cmd.GetShortName();
  CF_EXPECT(this->Setup(std::move(subprocess)));
  {
    // Force the destructor to run by moving it into a smaller scope.
    // This is necessary to close the write end of the pipe.
    Command forceDelete = std::move(cmd);
  }

  param.callback_();
  CF_EXPECT(this->Wait());
  for (auto& thread : threads_) {
    if (thread->joinable()) {
      thread->join();
    }
  }
  CF_EXPECT(!io_error,
            "IO error communicating with " << cmd_short_name);
  return output;
}

}  // namespace cuttlefish
