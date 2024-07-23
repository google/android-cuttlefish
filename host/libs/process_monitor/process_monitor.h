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

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
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

    Properties& StraceCommands(std::set<std::string>) &;
    Properties StraceCommands(std::set<std::string>) &&;

    Properties& StraceLogDir(std::string) &;
    Properties StraceLogDir(std::string) &&;

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
    std::set<std::string> strace_commands_;
    std::string strace_log_dir_;

    friend class ProcessMonitor;
  };
  /*
   * secure_env_fd is to send suspend/resume commands to secure_env.
   */
  ProcessMonitor(Properties&&, const SharedFD& secure_env_fd);

  // Start all processes given by AddCommand.
  Result<void> StartAndMonitorProcesses();
  // Stops all monitored subprocesses.
  Result<void> StopMonitoredProcesses();
  // Suspend all host subprocesses
  Result<void> SuspendMonitoredProcesses();
  // Resume all host subprocesses
  Result<void> ResumeMonitoredProcesses();

 private:
  Result<void> StartSubprocesses(Properties& properties);
  Result<void> MonitorRoutine();
  Result<void> ReadMonitorSocketLoop(std::atomic_bool&);
  /*
   * The child run_cvd process suspends the host processes
   */
  Result<void> SuspendHostProcessesImpl();
  /*
   * The child run_cvd process resumes the host processes
   */
  Result<void> ResumeHostProcessesImpl();

  Properties properties_;
  const SharedFD channel_to_secure_env_;
  pid_t monitor_;
  SharedFD parent_monitor_socket_;
  SharedFD child_monitor_socket_;

  /*
   * The lock that should be acquired when multiple threads
   * access to properties_. Currently, used by the child
   * run_cvd process that runs MonitorRoutine()
   */
  std::mutex properties_mutex_;
};

}  // namespace cuttlefish
