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
#include <atomic>
#include <future>
#include <memory>
#include <thread>

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

namespace {

struct ParentToChildMessage {
  bool stop;
};

void LogSubprocessExit(const std::string& name, pid_t pid, int wstatus) {
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

void LogSubprocessExit(const std::string& name, const siginfo_t& infop) {
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

Result<void> StartSubprocesses(std::vector<MonitorEntry>& entries) {
  LOG(DEBUG) << "Starting monitored subprocesses";
  for (auto& monitored : entries) {
    LOG(INFO) << monitored.cmd->GetShortName();
    auto options = SubprocessOptions().InGroup(true);
    monitored.proc.reset(new Subprocess(monitored.cmd->Start(options)));
    CF_EXPECT(monitored.proc->Started(), "Failed to start subprocess");
  }
  return {};
}

Result<void> ReadMonitorSocketLoopForStop(std::atomic_bool& running,
                                          SharedFD& monitor_socket) {
  LOG(DEBUG) << "Waiting for a `stop` message from the parent";
  while (running.load()) {
    ParentToChildMessage message;
    CF_EXPECT(ReadExactBinary(monitor_socket, &message) == sizeof(message),
              "Could not read message from parent");
    if (message.stop) {
      running.store(false);
      // Wake up the wait() loop by giving it an exited child process
      if (fork() == 0) {
        std::exit(0);
      }
    }
  }
  return {};
}

Result<void> MonitorLoop(const std::atomic_bool& running,
                         const bool restart_subprocesses,
                         std::vector<MonitorEntry>& monitored) {
  while (running.load()) {
    int wstatus;
    pid_t pid = wait(&wstatus);
    int error_num = errno;
    CF_EXPECT(pid != -1, "Wait failed: " << strerror(error_num));
    if (!WIFSIGNALED(wstatus) && !WIFEXITED(wstatus)) {
      LOG(DEBUG) << "Unexpected status from wait: " << wstatus
                  << " for pid " << pid;
      continue;
    }
    if (!running.load()) {  // Avoid extra restarts near the end
      break;
    }
    auto matches = [pid](const auto& it) { return it.proc->pid() == pid; };
    auto it = std::find_if(monitored.begin(), monitored.end(), matches);
    if (it == monitored.end()) {
      LogSubprocessExit("(unknown)", pid, wstatus);
    } else {
      LogSubprocessExit(it->cmd->GetShortName(), it->proc->pid(), wstatus);
      if (restart_subprocesses) {
        auto options = SubprocessOptions().InGroup(true);
        it->proc.reset(new Subprocess(it->cmd->Start(options)));
      } else {
        bool is_critical = it->is_critical;
        monitored.erase(it);
        if (running.load() && is_critical) {
          LOG(ERROR) << "Stopping all monitored processes due to unexpected "
                        "exit of critical process";
          Command stop_cmd(StopCvdBinary());
          stop_cmd.Start();
        }
      }
    }
  }
  return {};
}

Result<void> StopSubprocesses(std::vector<MonitorEntry>& monitored) {
  LOG(DEBUG) << "Stopping monitored subprocesses";
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
  CF_EXPECT(stopped == monitored.size(), "Didn't stop all subprocesses");
  return {};
}

}  // namespace

ProcessMonitor::Properties& ProcessMonitor::Properties::RestartSubprocesses(
    bool r) & {
  restart_subprocesses_ = r;
  return *this;
}

ProcessMonitor::Properties ProcessMonitor::Properties::RestartSubprocesses(
    bool r) && {
  return std::move(RestartSubprocesses(r));
}

ProcessMonitor::Properties& ProcessMonitor::Properties::AddCommand(
    MonitorCommand cmd) & {
  entries_.emplace_back(std::move(cmd.command), cmd.is_critical);
  return *this;
}

ProcessMonitor::Properties ProcessMonitor::Properties::AddCommand(
    MonitorCommand cmd) && {
  return std::move(AddCommand(std::move(cmd)));
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
    auto monitor_result = MonitorRoutine();
    if (!monitor_result.ok()) {
      LOG(ERROR) << "Monitoring processes failed:\n"
                 << monitor_result.error().Message();
      LOG(DEBUG) << "Monitoring processes failed:\n"
                 << monitor_result.error().Trace();
    }
    std::exit(monitor_result.ok() ? 0 : 1);
  } else {
    client_pipe->Close();
    monitor_socket_ = host_pipe;
    return {};
  }
}

Result<void> ProcessMonitor::MonitorRoutine() {
  // Make this process a subreaper to reliably catch subprocess exits.
  // See https://man7.org/linux/man-pages/man2/prctl.2.html
  prctl(PR_SET_CHILD_SUBREAPER, 1);
  prctl(PR_SET_PDEATHSIG, SIGHUP);  // Die when parent dies

  LOG(DEBUG) << "Monitoring subprocesses";
  StartSubprocesses(properties_.entries_);

  std::atomic_bool running(true);
  auto parent_comms =
      std::async(std::launch::async, ReadMonitorSocketLoopForStop,
                 std::ref(running), std::ref(monitor_socket_));

  MonitorLoop(running, properties_.restart_subprocesses_, properties_.entries_);
  CF_EXPECT(parent_comms.get(), "Should have exited if monitoring stopped");

  StopSubprocesses(properties_.entries_);
  LOG(DEBUG) << "Done monitoring subprocesses";
  return {};
}

}  // namespace cuttlefish
