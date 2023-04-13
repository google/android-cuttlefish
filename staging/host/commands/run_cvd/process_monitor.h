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
#include <utility>
#include <vector>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/command_source.h"

namespace cuttlefish {

struct MonitorEntry {
  std::unique_ptr<Command> cmd;
  std::unique_ptr<Subprocess> proc;
  bool is_critical;

  MonitorEntry(Command command, bool is_critical)
      : cmd(new Command(std::move(command))), is_critical(is_critical) {}
};

// Launches and keeps track of subprocesses, decides response if they
// unexpectedly exit
class ProcessMonitor {
 public:
  class Properties {
   public:
    Properties& RestartSubprocesses(bool) &;
    Properties RestartSubprocesses(bool) &&;

    Properties& AddCommand(MonitorCommand) &;
    Properties AddCommand(MonitorCommand) &&;

    template <typename T>
    Properties& AddCommands(T commands) & {
      for (auto& command : commands) {
        AddCommand(std::move(command));
      }
      return *this;
    }

    template <typename T>
    Properties AddCommands(T commands) && {
      return std::move(AddCommands(std::move(commands)));
    }

   private:
    bool restart_subprocesses_;
    std::vector<MonitorEntry> entries_;

    friend class ProcessMonitor;
  };
  ProcessMonitor(Properties&&);

  // Start all processes given by AddCommand.
  Result<void> StartAndMonitorProcesses();
  // Stops all monitored subprocesses.
  Result<void> StopMonitoredProcesses();

 private:
  Result<void> MonitorRoutine();

  Properties properties_;
  pid_t monitor_;
  SharedFD monitor_socket_;
};

}  // namespace cuttlefish
