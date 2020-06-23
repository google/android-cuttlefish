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

#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include <map>

#include <android-base/logging.h>

#include "common/libs/fs/shared_select.h"
#include "host/commands/run_cvd/process_monitor.h"

namespace cuttlefish {

namespace {

void NotifyThread(SharedFD fd) {
  // The restarter thread is (likely) blocked on a call to select, to make it
  // wake up and do some work we write something (anything, the content is not
  // important) into the main side of the socket pair so that the call to select
  // returns and the notification fd (restarter side of the socket pair) is
  // marked as ready to read.
  char buffer = 'a';
  fd->Write(&buffer, sizeof(buffer));
}

void ConsumeNotifications(SharedFD fd) {
  // Once the starter thread is waken up due to a notification, the calls to
  // select will continue to return immediately unless we read what was written
  // on the main side of the socket pair. More than one notification can
  // accumulate before the restarter thread consumes them, so we attempt to read
  // more than it's written to consume them all at once. In the unlikely case of
  // more than 8 notifications acummulating we simply read the first 8 and have
  // another iteration on the restarter thread loop.
  char buffer[8];
  fd->Read(buffer, sizeof(buffer));
}

}  // namespace

ProcessMonitor::ProcessMonitor() {
  if (!SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &thread_comm_main_,
                            &thread_comm_monitor_)) {
    LOG(ERROR) << "Unable to create restarter communication socket pair: "
               << strerror(errno);
    return;
  }
  monitor_thread_ = std::thread([this]() { MonitorRoutine(); });
}

void ProcessMonitor::StartSubprocess(Command cmd, OnSocketReadyCb callback) {
  cuttlefish::SubprocessOptions options;
  options.InGroup(true);
  options.WithControlSocket(true);
  auto proc = cmd.Start(options);
  if (!proc.Started()) {
    LOG(ERROR) << "Failed to start process";
    return;
  }
  MonitorExistingSubprocess(std::move(cmd), std::move(proc), callback);
}

void ProcessMonitor::MonitorExistingSubprocess(Command cmd, Subprocess proc,
                                               OnSocketReadyCb callback) {
  {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    monitored_processes_.push_back(MonitorEntry());
    auto& entry = monitored_processes_.back();
    entry.cmd.reset(new Command(std::move(cmd)));
    entry.proc.reset(new Subprocess(std::move(proc)));
    entry.on_control_socket_ready_cb = callback;
  }
  // Wake the restarter thread up so that it starts monitoring this subprocess
  // Do this after releasing the lock so that the restarter thread is free to
  // begin work as soon as select returns.
  NotifyThread(thread_comm_main_);
}

bool ProcessMonitor::StopMonitoredProcesses() {
  // Because the mutex is held while this function executes, the restarter
  // thread is kept blocked and by the time it resumes execution there are no
  // more processes to monitor
  std::lock_guard<std::mutex> lock(processes_mutex_);
  bool result = true;
  // Processes were started in the order they appear in the vector, stop them in
  // reverse order for symmetry.
  for (auto entry_it = monitored_processes_.rbegin();
       entry_it != monitored_processes_.rend(); ++entry_it) {
    auto& entry = *entry_it;
    result = result && entry.proc->Stop();
  }
  // Wait for all processes to actually exit.
  for (auto& entry : monitored_processes_) {
    // Most processes are being killed by signals, calling Wait(void) would be
    // too verbose on the logs.
    int wstatus;
    auto ret = entry.proc->Wait(&wstatus, 0);
    if (ret < 0) {
      LOG(WARNING) << "Failed to wait for process "
                   << entry.cmd->GetShortName();
    }
  }
  // Clear the list to ensure they are not started again
  monitored_processes_.clear();
  return result;
}

bool ProcessMonitor::RestartOnExitCb(MonitorEntry* entry) {
  // Make sure the process actually exited
  char buffer[16];
  auto bytes_read = entry->proc->control_socket()->Read(buffer, sizeof(buffer));
  if (bytes_read > 0) {
    LOG(WARNING) << "Subprocess " << entry->cmd->GetShortName() << " wrote "
                 << bytes_read
                 << " bytes on the control socket, this is unexpected";
    // The process may not have exited, continue monitoring without restarting
    return true;
  }

  LOG(INFO) << "Detected exit of monitored subprocess";
  // Make sure the subprocess isn't left in a zombie state, and that the
  // pid is logged
  int wstatus;
  auto wait_ret = TEMP_FAILURE_RETRY(entry->proc->Wait(&wstatus, 0));
  // None of the error conditions specified on waitpid(2) apply
  assert(wait_ret > 0);
  if (WIFEXITED(wstatus)) {
    LOG(INFO) << "Subprocess " << entry->cmd->GetShortName() << " (" << wait_ret
              << ") has exited with exit code " << WEXITSTATUS(wstatus);
  } else if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Subprocess " << entry->cmd->GetShortName() << " ("
               << wait_ret
               << ") was interrupted by a signal: " << WTERMSIG(wstatus);
  } else {
    LOG(INFO) << "subprocess " << entry->cmd->GetShortName() << " (" << wait_ret
              << ") has exited for unknown reasons";
  }
  cuttlefish::SubprocessOptions options;
  options.WithControlSocket(true);
  entry->proc.reset(new Subprocess(entry->cmd->Start(options)));
  return true;
}

bool ProcessMonitor::DoNotMonitorCb(MonitorEntry*) { return false; }

void ProcessMonitor::MonitorRoutine() {
  LOG(DEBUG) << "Started monitoring subprocesses";
  do {
    SharedFDSet read_set;
    read_set.Set(thread_comm_monitor_);
    {
      std::lock_guard<std::mutex> lock(processes_mutex_);
      for (auto& monitored_process : monitored_processes_) {
        auto control_socket = monitored_process.proc->control_socket();
        if (!control_socket->IsOpen()) {
          LOG(ERROR) << "The control socket for "
                     << monitored_process.cmd->GetShortName()
                     << " is closed, it's effectively NOT being monitored";
        }
        read_set.Set(control_socket);
      }
    }
    // We can't call select while holding the lock as it would lead to a
    // deadlock (restarter thread waiting for notifications from main thread,
    // main thread waiting for the lock)
    int num_fds = cuttlefish::Select(&read_set, nullptr, nullptr, nullptr);
    if (num_fds < 0) {
      LOG(ERROR) << "Select call returned error on restarter thread: "
                 << strerror(errno);
    }
    if (num_fds > 0) {
      // Try the communication fd, it's the most likely to be set
      if (read_set.IsSet(thread_comm_monitor_)) {
        --num_fds;
        ConsumeNotifications(thread_comm_monitor_);
      }
    }
    {
      std::lock_guard<std::mutex> lock(processes_mutex_);
      // Keep track of the number of file descriptors ready for read, chances
      // are we don't need to go over the entire list of subprocesses
      auto it = monitored_processes_.begin();
      while (it != monitored_processes_.end()) {
        auto control_socket = it->proc->control_socket();
        bool keep_monitoring = true;
        if (read_set.IsSet(control_socket)) {
          --num_fds;
          keep_monitoring = it->on_control_socket_ready_cb(&(*it));
        }
        if (keep_monitoring) {
          ++it;
        } else {
          it = monitored_processes_.erase(it);
        }
      }
    }
    assert(num_fds == 0);
  } while (true);
}

}  // namespace cuttlefish
