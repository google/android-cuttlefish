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

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <common/libs/utils/subprocess.h>

namespace cuttlefish {

struct MonitorEntry;
using OnSocketReadyCb = std::function<bool(MonitorEntry*)>;

struct MonitorEntry {
  std::unique_ptr<Command> cmd;
  std::unique_ptr<Subprocess> proc;
  OnSocketReadyCb on_control_socket_ready_cb;
};

// Keeps track of launched subprocesses, restarts them if they unexpectedly exit
class ProcessMonitor {
 public:
  ProcessMonitor();
  // Starts a managed subprocess with a controlling socket.
  // The on_control_socket_ready_cb callback will be called when data is ready
  // to be read from the socket or the subprocess has ended. No member functions
  // of the process monitor object should be called from the callback as it may
  // lead to a deadlock. If the callback returns false the subprocess will no
  // longer be monitored.
  void StartSubprocess(Command cmd, OnSocketReadyCb on_control_socket_ready_cb);
  // Monitors an already started subprocess
  void MonitorExistingSubprocess(Command cmd, Subprocess sub_process,
                                 OnSocketReadyCb on_control_socket_ready_cb);
  // Stops all monitored subprocesses.
  bool StopMonitoredProcesses();
  static bool RestartOnExitCb(MonitorEntry* entry);
  static bool DoNotMonitorCb(MonitorEntry* entry);

 private:
  void MonitorRoutine();

  std::vector<MonitorEntry> monitored_processes_;
  // Used for communication with the restarter thread
  cuttlefish::SharedFD thread_comm_main_, thread_comm_monitor_;
  std::thread monitor_thread_;
  // Protects access to the monitored_processes_
  std::mutex processes_mutex_;
};
}  // namespace cuttlefish
