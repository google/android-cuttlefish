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

#include "host/commands/run_cvd/process_monitor.h"

#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include <algorithm>
#include <future>
#include <thread>

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_select.h"

namespace cuttlefish {

struct ParentToChildMessage {
  bool stop;
};

ProcessMonitor::Properties& ProcessMonitor::Properties::RestartSubprocesses(
    bool r) & {
  restart_subprocesses_ = r;
  return *this;
}

ProcessMonitor::Properties ProcessMonitor::Properties::RestartSubprocesses(
    bool r) && {
  restart_subprocesses_ = r;
  return std::move(*this);
}

ProcessMonitor::Properties& ProcessMonitor::Properties::AddCommand(
    Command cmd) & {
  auto& entry = entries_.emplace_back();
  entry.cmd.reset(new Command(std::move(cmd)));
  return *this;
}

ProcessMonitor::Properties ProcessMonitor::Properties::AddCommand(
    Command cmd) && {
  auto& entry = entries_.emplace_back();
  entry.cmd.reset(new Command(std::move(cmd)));
  return std::move(*this);
}

ProcessMonitor::ProcessMonitor(ProcessMonitor::Properties&& properties)
    : properties_(std::move(properties)), monitor_(-1) {}

Result<void> ProcessMonitor::StopMonitoredProcesses() {
  CF_EXPECT(monitor_ != -1, "The monitor process has already exited.");
  CF_EXPECT(monitor_socket_->IsOpen(), "The monitor socket is already closed");
  ParentToChildMessage message;
  message.stop = true;
  CF_EXPECT(WriteAllBinary(monitor_socket_, &message) == sizeof(message),
            "Failed to communicate with monitor socket: "
                << monitor_socket_->StrError());

  pid_t last_monitor = monitor_;
  monitor_ = -1;
  monitor_socket_->Close();
  int wstatus;
  CF_EXPECT(waitpid(last_monitor, &wstatus, 0) == last_monitor,
            "Failed to wait for monitor process");
  CF_EXPECT(!WIFSIGNALED(wstatus), "Monitor process exited due to a signal");
  CF_EXPECT(WIFEXITED(wstatus), "Monitor process exited for unknown reasons");
  CF_EXPECT(WEXITSTATUS(wstatus) == 0,
            "Monitor process exited with code " << WEXITSTATUS(wstatus));
  return {};
}

Result<void> ProcessMonitor::StartAndMonitorProcesses() {
  CF_EXPECT(monitor_ == -1, "The monitor process was already started");
  CF_EXPECT(!monitor_socket_->IsOpen(), "Monitor socket was already opened");

  SharedFD client_pipe, host_pipe;
  CF_EXPECT(SharedFD::Pipe(&client_pipe, &host_pipe),
            "Could not create the monitor socket.");
  monitor_ = fork();
  if (monitor_ == 0) {
    monitor_socket_ = client_pipe;
    host_pipe->Close();
    auto monitor = MonitorRoutine();
    if (!monitor.ok()) {
      LOG(ERROR) << "Monitoring processes failed:\n" << monitor.error();
    }
    std::exit(monitor.ok() ? 0 : 1);
  } else {
    client_pipe->Close();
    monitor_socket_ = host_pipe;
    return {};
  }
}

static void LogSubprocessExit(const std::string& name, pid_t pid, int wstatus) {
  LOG(INFO) << "Detected unexpected exit of monitored subprocess " << name;
  if (WIFEXITED(wstatus)) {
    LOG(INFO) << "Subprocess " << name << " (" << pid
              << ") has exited with exit code " << WEXITSTATUS(wstatus);
  } else if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Subprocess " << name << " (" << pid
               << ") was interrupted by a signal: " << WTERMSIG(wstatus);
  } else {
    LOG(INFO) << "subprocess " << name << " (" << pid
              << ") has exited for unknown reasons";
  }
}

static void LogSubprocessExit(const std::string& name, const siginfo_t& infop) {
  LOG(INFO) << "Detected unexpected exit of monitored subprocess " << name;
  if (infop.si_code == CLD_EXITED) {
    LOG(INFO) << "Subprocess " << name << " (" << infop.si_pid
              << ") has exited with exit code " << infop.si_status;
  } else if (infop.si_code == CLD_KILLED) {
    LOG(ERROR) << "Subprocess " << name << " (" << infop.si_pid
               << ") was interrupted by a signal: " << infop.si_status;
  } else {
    LOG(INFO) << "subprocess " << name << " (" << infop.si_pid
              << ") has exited for unknown reasons (code = " << infop.si_code
              << ", status = " << infop.si_status << ")";
  }
}

Result<void> ProcessMonitor::MonitorRoutine() {
  // Make this process a subreaper to reliably catch subprocess exits.
  // See https://man7.org/linux/man-pages/man2/prctl.2.html
  prctl(PR_SET_CHILD_SUBREAPER, 1);
  prctl(PR_SET_PDEATHSIG, SIGHUP); // Die when parent dies

  LOG(DEBUG) << "Starting monitoring subprocesses";
  for (auto& monitored : properties_.entries_) {
    LOG(INFO) << monitored.cmd->GetShortName();
    auto options = SubprocessOptions().InGroup(true);
    monitored.proc.reset(new Subprocess(monitored.cmd->Start(options)));
    CF_EXPECT(monitored.proc->Started(), "Failed to start process");
  }

  bool running = true;
  auto policy = std::launch::async;
  auto parent_comms = std::async(policy, [&running, this]() -> Result<void> {
    LOG(DEBUG) << "Waiting for a `stop` message from the parent.";
    while (running) {
      ParentToChildMessage message;
      CF_EXPECT(ReadExactBinary(monitor_socket_, &message) == sizeof(message),
                "Could not read message from parent.");
      if (message.stop) {
        running = false;
        // Wake up the wait() loop by giving it an exited child process
        if (fork() == 0) {
          std::exit(0);
        }
      }
    }
    return {};
  });

  auto& monitored = properties_.entries_;

  LOG(DEBUG) << "Monitoring subprocesses";
  while(running) {
    int wstatus;
    pid_t pid = wait(&wstatus);
    int error_num = errno;
    CF_EXPECT(pid != -1, "Wait failed: " << strerror(error_num));
    if (!WIFSIGNALED(wstatus) && !WIFEXITED(wstatus)) {
      LOG(DEBUG) << "Unexpected status from wait: " << wstatus
                  << " for pid " << pid;
      continue;
    }
    if (!running) { // Avoid extra restarts near the end
      break;
    }
    auto matches = [pid](const auto& it) { return it.proc->pid() == pid; };
    auto it = std::find_if(monitored.begin(), monitored.end(), matches);
    if (it == monitored.end()) {
      LogSubprocessExit("(unknown)", pid, wstatus);
    } else {
      LogSubprocessExit(it->cmd->GetShortName(), it->proc->pid(), wstatus);
      if (properties_.restart_subprocesses_) {
        auto options = SubprocessOptions().InGroup(true);
        it->proc.reset(new Subprocess(it->cmd->Start(options)));
      } else {
        properties_.entries_.erase(it);
      }
    }
  }

  CF_EXPECT(parent_comms.get());  // Should have exited if `running` is false
  auto stop = [](const auto& it) {
    auto stop_result = it.proc->Stop();
    if (stop_result == StopperResult::kStopFailure) {
      LOG(WARNING) << "Error in stopping \"" << it.cmd->GetShortName() << "\"";
      return false;
    }
    siginfo_t infop;
    auto success = it.proc->Wait(&infop, WEXITED);
    if (success < 0) {
      LOG(WARNING) << "Failed to wait for process " << it.cmd->GetShortName();
      return false;
    }
    if (stop_result == StopperResult::kStopCrash) {
      LogSubprocessExit(it.cmd->GetShortName(), infop);
    }
    return true;
  };
  // Processes were started in the order they appear in the vector, stop them in
  // reverse order for symmetry.
  size_t stopped = std::count_if(monitored.rbegin(), monitored.rend(), stop);
  LOG(DEBUG) << "Done monitoring subprocesses";
  CF_EXPECT(stopped == monitored.size(), "Didn't stop all subprocesses");
  return {};
}

}  // namespace cuttlefish
