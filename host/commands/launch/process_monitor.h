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

namespace cvd {
struct MonitorEntry {
  std::unique_ptr<Command> cmd;
  std::unique_ptr<Subprocess> proc;
};

// Keeps track of launched subprocesses, restarts them if they unexpectedly exit
class ProcessMonitor {
 public:
  ProcessMonitor();
  void StartSubprocess(Command cmd, bool restart_on_exit = false);

 private:
  void RestarterRoutine();

  std::vector<MonitorEntry> monitored_processes_;
  // Used for communication with the restarter thread
  cvd::SharedFD thread_comm_main_, thread_comm_restarter_;
  std::thread restarter_;
  // Protects access to the monitored_processes_
  std::mutex processes_mutex_;
};
}  // namespace cvd
