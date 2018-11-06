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

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include <map>

#include <glog/logging.h>

#include "common/libs/fs/shared_select.h"
#include "host/commands/launch/process_monitor.h"

namespace cvd {

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

void WaitForSubprocessAndRestartIt(cvd::MonitorEntry* monitor_entry) {
  // In the future we may want to read from the fd, but for now we
  // assume it just needs restarting
  LOG(INFO) << "Detected exit of monitored subprocess";
  // Make sure the subprocess isn't left in a zombie state, and that the
  // pid is logged
  int wstatus;
  auto wait_ret = TEMP_FAILURE_RETRY(monitor_entry->proc->Wait(&wstatus, 0));
  // None of the error conditions specified on waitpid(2) apply
  assert(wait_ret > 0);
  if (WIFEXITED(wstatus)) {
    LOG(INFO) << "Subprocess " << monitor_entry->cmd->GetShortName() << " ("
              << wait_ret << ") has exited with exit code "
              << WEXITSTATUS(wstatus);
  } else if (WIFSIGNALED(wstatus)) {
    LOG(ERROR) << "Subprocess " << monitor_entry->cmd->GetShortName() << " ("
               << wait_ret << ") was interrupted by a signal: "
               << WTERMSIG(wstatus);
  } else {
    LOG(INFO) << "subprocess " << monitor_entry->cmd->GetShortName() << " ("
               << wait_ret << ") has exited for unknown reasons";
  }
  monitor_entry->proc.reset(new Subprocess(monitor_entry->cmd->Start(true)));
}

}  // namespace

ProcessMonitor::ProcessMonitor() {
  if (!SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &thread_comm_main_,
                            &thread_comm_restarter_)) {
    LOG(ERROR) << "Unable to create restarter communication socket pair: "
               << strerror(errno);
    return;
  }
  restarter_ = std::thread([this]() { RestarterRoutine(); });
}

void ProcessMonitor::StartSubprocess(Command cmd, bool restart_on_exit) {
  auto proc = cmd.Start(true);
  if (!proc.Started()) {
    LOG(ERROR) << "Failed to start process";
    return;
  }
  if (restart_on_exit) {
    {
      std::lock_guard<std::mutex> lock(processes_mutex_);
      monitored_processes_.push_back(MonitorEntry());
      auto& entry = monitored_processes_.back();
      entry.cmd.reset(new Command(std::move(cmd)));
      entry.proc.reset(new Subprocess(std::move(proc)));
    }
    // Wake the restarter thread up so that it starts monitoring this subprocess
    // Do this after releasing the lock so that the restarter thread is free to
    // begin work as soon as select returns.
    NotifyThread(thread_comm_main_);
  }
}

void ProcessMonitor::RestarterRoutine() {
  LOG(INFO) << "Started monitoring subprocesses";
  do {
    SharedFDSet read_set;
    read_set.Set(thread_comm_restarter_);
    {
      std::lock_guard<std::mutex> lock(processes_mutex_);
      for (auto& monitored_process: monitored_processes_) {
        auto control_socket = monitored_process.proc->control_socket();
        if (!control_socket->IsOpen())  {
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
    int num_fds = cvd::Select(&read_set, nullptr, nullptr, nullptr);
    if (num_fds < 0) {
      LOG(ERROR) << "Select call returned error on restarter thread: "
                 << strerror(errno);
    }
    if (num_fds > 0) {
      // Try the communication fd, it's the most likely to be set
      if (read_set.IsSet(thread_comm_restarter_)) {
        --num_fds;
        ConsumeNotifications(thread_comm_restarter_);
      }
    }
    {
      std::lock_guard<std::mutex> lock(processes_mutex_);
      // Keep track of the number of file descriptors ready for read, chances
      // are we don't need to go over the entire list of subprocesses
      for (size_t idx = 0; idx < monitored_processes_.size() && num_fds > 0;
           ++idx) {
        auto monitor_entry = &monitored_processes_[idx];
        auto control_socket = monitor_entry->proc->control_socket();
        if (read_set.IsSet(control_socket)) {
          --num_fds;
          WaitForSubprocessAndRestartIt(monitor_entry);
        }
      }
    }
    assert(num_fds == 0);
  } while (true);
}

}  // namespace cvd
