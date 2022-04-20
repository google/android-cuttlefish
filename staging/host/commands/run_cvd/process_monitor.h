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

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"

namespace cuttlefish {

struct MonitorEntry {
  std::unique_ptr<Command> cmd;
  std::unique_ptr<Subprocess> proc;
};

// Keeps track of launched subprocesses, restarts them if they unexpectedly exit
class ProcessMonitor {
 public:
  ProcessMonitor(bool restart_subprocesses);
  // Adds a command to the list of commands to be run and monitored. Can only be
  // called before StartAndMonitorProcesses is called.
  Result<void> AddCommand(Command cmd);
  template <typename T>
  Result<void> AddCommands(T&& commands) {
    for (auto& command : commands) {
      CF_EXPECT(AddCommand(std::move(command)));
    }
    return {};
  }

  // Start all processes given by AddCommand.
  Result<void> StartAndMonitorProcesses();
  // Stops all monitored subprocesses.
  Result<void> StopMonitoredProcesses();

 private:
  Result<void> MonitorRoutine();

  bool restart_subprocesses_;
  std::vector<MonitorEntry> monitored_processes_;
  pid_t monitor_;
  SharedFD monitor_socket_;
};
}  // namespace cuttlefish
