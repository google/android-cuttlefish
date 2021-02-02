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
#include <thread>

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_select.h"

namespace cuttlefish {

struct ParentToChildMessage {
  bool stop;
};

ProcessMonitor::ProcessMonitor(bool restart_subprocesses)
    : restart_subprocesses_(restart_subprocesses), monitor_(-1) {
}

void ProcessMonitor::AddCommand(Command cmd) {
  CHECK(monitor_ == -1) << "The monitor process is already running.";
  CHECK(!monitor_socket_->IsOpen()) << "The monitor socket is already open.";

  monitored_processes_.push_back(MonitorEntry());
  auto& entry = monitored_processes_.back();
  entry.cmd.reset(new Command(std::move(cmd)));
}

bool ProcessMonitor::StopMonitoredProcesses() {
  if (monitor_ == -1) {
    LOG(ERROR) << "The monitor process is already dead.";
    return false;
  }
  if (!monitor_socket_->IsOpen()) {
    LOG(ERROR) << "The monitor socket is already closed.";
    return false;
  }
  ParentToChildMessage message;
  message.stop = true;
  if (WriteAllBinary(monitor_socket_, &message) != sizeof(message)) {
    LOG(ERROR) << "Failed to communicate with monitor socket: "
                << monitor_socket_->StrError();
    return false;
  }
  pid_t last_monitor = monitor_;
  monitor_ = -1;
  monitor_socket_->Close();
  int wstatus;
  if (waitpid(last_monitor, &wstatus, 0) != last_monitor) {
    LOG(ERROR) << "Failed to wait for monitor process";
    return false;
  }
  if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Monitor process exited due to a signal";
    return false;
  }
  if (!WIFEXITED(wstatus)) {
    LOG(ERROR) << "Monitor process exited for unknown reasons";
    return false;
  }
  if (WEXITSTATUS(wstatus) != 0) {
    LOG(ERROR) << "Monitor process exited with code " << WEXITSTATUS(wstatus);
    return false;
  }
  return true;
}

bool ProcessMonitor::StartAndMonitorProcesses() {
  if (monitor_ != -1) {
    LOG(ERROR) << "The monitor process was already started";
    return false;
  }
  if (monitor_socket_->IsOpen()) {
    LOG(ERROR) << "The monitor socket was already opened.";
    return false;
  }
  SharedFD client_pipe, host_pipe;
  if (!SharedFD::Pipe(&client_pipe, &host_pipe)) {
    LOG(ERROR) << "Could not create the monitor socket.";
    return false;
  }
  monitor_ = fork();
  if (monitor_ == 0) {
    monitor_socket_ = client_pipe;
    host_pipe->Close();
    std::exit(MonitorRoutine() ? 0 : 1);
  } else {
    client_pipe->Close();
    monitor_socket_ = host_pipe;
    return true;
  }
}

static void LogSubprocessExit(const std::string& name, pid_t pid, int wstatus) {
  LOG(INFO) << "Detected exit of monitored subprocess " << name;
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

bool ProcessMonitor::MonitorRoutine() {
  // Make this process a subreaper to reliably catch subprocess exits.
  // See https://man7.org/linux/man-pages/man2/prctl.2.html
  prctl(PR_SET_CHILD_SUBREAPER, 1);
  prctl(PR_SET_PDEATHSIG, SIGHUP); // Die when parent dies

  LOG(DEBUG) << "Starting monitoring subprocesses";
  for (auto& monitored : monitored_processes_) {
    cuttlefish::SubprocessOptions options;
    options.InGroup(true);
    monitored.proc.reset(new Subprocess(monitored.cmd->Start(options)));
    CHECK(monitored.proc->Started()) << "Failed to start process";
  }

  bool running = true;
  std::thread parent_comms_thread([&running, this]() {
    LOG(DEBUG) << "Waiting for a `stop` message from the parent.";
    while (running) {
      ParentToChildMessage message;
      CHECK(ReadExactBinary(monitor_socket_, &message) == sizeof(message))
          << "Could not read message from parent.";
      if (message.stop) {
        running = false;
        // Wake up the wait() loop by giving it an exited child process
        if (fork() == 0) {
          std::exit(0);
        }
      }
    }
  });

  auto& monitored = monitored_processes_;

  LOG(DEBUG) << "Monitoring subprocesses";
  while(running) {
    int wstatus;
    pid_t pid = wait(&wstatus);
    int error_num = errno;
    CHECK(pid != -1) << "Wait failed: " << strerror(error_num);
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
      if (restart_subprocesses_) {
        cuttlefish::SubprocessOptions options;
        options.InGroup(true);
        it->proc.reset(new Subprocess(it->cmd->Start(options)));
      } else {
        monitored_processes_.erase(it);
      }
    }
  }

  parent_comms_thread.join(); // Should have exited if `running` is false
  // Processes were started in the order they appear in the vector, stop them in
  // reverse order for symmetry.
  auto stop = [](const auto& it) {
    if (!it.proc->Stop()) {
      LOG(WARNING) << "Error in stopping \"" << it.cmd->GetShortName() << "\"";
      return false;
    }
    int wstatus = 0;
    auto ret = it.proc->Wait(&wstatus, 0);
    if (ret < 0) {
      LOG(WARNING) << "Failed to wait for process " << it.cmd->GetShortName();
      return false;
    }
    return true;
  };
  size_t stopped = std::count_if(monitored.rbegin(), monitored.rend(), stop);
  LOG(DEBUG) << "Done monitoring subprocesses";
  return stopped == monitored.size();
}

}  // namespace cuttlefish
