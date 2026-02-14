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

#include "cuttlefish/host/libs/process_monitor/process_monitor.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/transport/channel.h"
#include "cuttlefish/common/libs/transport/channel_sharedfd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/command_util/util.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using transport::Channel;
using transport::CreateMessage;
using transport::ManagedMessage;

enum ParentToChildMessageType : uint8_t {
  kStop = 1,
  kHostResume = 2,
  kHostSuspend = 3,
  kError = 4,
};

enum ChildToParentResponseType : uint8_t {
  kSuccess = 0,
  kFailure = 1,
};

Result<void> SendEmptyRequest(Channel& channel, uint32_t type) {
  ManagedMessage message = CF_EXPECT(CreateMessage(type, false, 0));
  CF_EXPECT(channel.SendRequest(*message));
  return {};
}

Result<void> SendEmptyResponse(Channel& channel, uint32_t type) {
  ManagedMessage message = CF_EXPECT(CreateMessage(type, true, 0));
  CF_EXPECT(channel.SendResponse(*message));
  return {};
}

void LogSubprocessExit(const std::string& name, pid_t pid, int wstatus) {
  LOG(INFO) << "Detected unexpected exit of monitored subprocess " << name;
  if (WIFEXITED(wstatus)) {
    LOG(INFO) << "Subprocess " << name << " (" << pid
              << ") has exited with exit code " << WEXITSTATUS(wstatus);
  } else if (WIFSIGNALED(wstatus)) {
    int sig_num = WTERMSIG(wstatus);
    LOG(ERROR) << "Subprocess " << name << " (" << pid
               << ") was interrupted by a signal '" << strsignal(sig_num)
               << "' (" << sig_num << ")";
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
               << ") was interrupted by a signal '"
               << strsignal(infop.si_status) << "' (" << infop.si_status << ")";
  } else {
    LOG(INFO) << "subprocess " << name << " (" << infop.si_pid
              << ") has exited for unknown reasons (code = " << infop.si_code
              << ", status = " << infop.si_status << ")";
  }
}

Result<void> MonitorLoop(std::atomic_bool& running,
                         std::mutex& properties_mutex,
                         const bool restart_subprocesses,
                         std::vector<MonitorEntry>& monitored) {
  while (running.load()) {
    int wstatus;
    pid_t pid = wait(&wstatus);
    CF_EXPECTF(pid != -1, "Wait failed: {}", StrError(errno));
    if (!WIFSIGNALED(wstatus) && !WIFEXITED(wstatus)) {
      VLOG(0) << "Unexpected status from wait: " << wstatus << " for pid "
              << pid;
      continue;
    }
    if (!running.load()) {  // Avoid extra restarts near the end
      break;
    }
    auto matches = [pid](const auto& it) { return it.proc->pid() == pid; };
    std::unique_lock lock(properties_mutex);
    auto it = std::find_if(monitored.begin(), monitored.end(), matches);
    if (it == monitored.end()) {
      LogSubprocessExit("(unknown)", pid, wstatus);
    } else {
      LogSubprocessExit(it->cmd->GetShortName(), it->proc->pid(), wstatus);
      if (restart_subprocesses) {
        auto options = SubprocessOptions().InGroup(true);
        // in the future, cmd->Start might not run exec()
        it->proc.reset(new Subprocess(it->cmd->Start(std::move(options))));
      } else {
        bool is_critical = it->is_critical;
        monitored.erase(it);
        if (running.load() && is_critical) {
          LOG(ERROR) << "Stopping all monitored processes due to unexpected "
                        "exit of critical process";
          running.store(false);
          break;
        }
      }
    }
  }
  return {};
}

Result<void> StopSubprocesses(std::vector<MonitorEntry>& monitored) {
  VLOG(0) << "Stopping monitored subprocesses";
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

Result<void> SuspendResumeImpl(std::vector<MonitorEntry>& monitor_entries,
                               std::mutex& properties_mutex,
                               const SharedFD& channel_to_secure_env,
                               const bool is_suspend,
                               transport::SharedFdChannel& socket) {
  std::lock_guard lock(properties_mutex);
  auto secure_env_itr = std::find_if(
      monitor_entries.begin(), monitor_entries.end(), [](MonitorEntry& entry) {
        auto prog_name = android::base::Basename(entry.cmd->Executable());
        return (prog_name == "secure_env");
      });
  if (secure_env_itr != monitor_entries.end()) {
    CF_EXPECT(channel_to_secure_env->IsOpen(),
              "channel to secure_env is not open.");
    run_cvd::ExtendedLauncherAction extended_action;
    if (is_suspend) {
      extended_action.mutable_suspend();
    } else {
      extended_action.mutable_resume();
    }
    CF_EXPECT(RunLauncherAction(channel_to_secure_env, extended_action,
                                std::nullopt));
  }

  for (const auto& entry : monitor_entries) {
    if (!entry.cmd) {
      LOG(ERROR) << "Monitor Entry has a nullptr for cmd.";
      continue;
    }
    if (!entry.proc) {
      LOG(ERROR) << "Monitor Entry has a nullptr for proc.";
      continue;
    }
    auto prog_name = android::base::Basename(entry.cmd->Executable());
    auto process_restart_bin =
        android::base::Basename(ProcessRestarterBinary());
    if (prog_name == "log_tee") {
      // Don't stop log_tee, we want to continue processing logs while
      // suspended.
      continue;
    }
    if (prog_name == "wmediumd") {
      // wmediumd should be running while openWRT is saved using the
      // guest snapshot logic
      continue;
    }
    if (prog_name == "secure_env") {
      // secure_env was handled above in a customized way
      continue;
    }
    if (absl::StartsWith(prog_name, "cf_vhost_user_")) {
      // vhost user backend processes need to continue handling requests from
      // the VMM, which should send them the suspend signal.
      continue;
    }

    if (process_restart_bin == prog_name) {
      if (is_suspend) {
        CF_EXPECT(entry.proc->SendSignal(SIGTSTP));
      } else {
        CF_EXPECT(entry.proc->SendSignal(SIGCONT));
      }
      continue;
    }
    if (is_suspend) {
      CF_EXPECT(entry.proc->SendSignalToGroup(SIGTSTP));
    } else {
      CF_EXPECT(entry.proc->SendSignalToGroup(SIGCONT));
    }
  }
  CF_EXPECT(SendEmptyResponse(socket, ChildToParentResponseType::kSuccess));
  return {};
}

}  // namespace

Result<void> ProcessMonitor::StartSubprocesses(
    ProcessMonitor::Properties& properties) {
  VLOG(0) << "Starting monitored subprocesses";
  for (auto& monitored : properties.entries_) {
    LOG(INFO) << "Starting monitored subprocess: "
              << monitored.cmd->GetShortName();
    auto options = SubprocessOptions().InGroup(true);
    std::string short_name = monitored.cmd->GetShortName();
    auto last_slash = short_name.find_last_of('/');
    if (last_slash != std::string::npos) {
      short_name = short_name.substr(last_slash + 1);
    }
    if (Contains(properties_.strace_commands_, short_name)) {
      options.Strace(properties.strace_log_dir_ + "/strace-" + short_name);
    }
    monitored.proc.reset(
        new Subprocess(monitored.cmd->Start(std::move(options))));
    CF_EXPECT(monitored.proc->Started(), "Failed to start subprocess");
  }
  return {};
}

Result<void> ProcessMonitor::ReadMonitorSocketLoop(std::atomic_bool& running) {
  VLOG(0) << "Waiting for a `stop` message from the parent";
  while (running.load()) {
    ManagedMessage message = CF_EXPECT(child_channel_->ReceiveMessage());
    if (message->command == ParentToChildMessageType::kStop) {
      running.store(false);
      // Wake up the wait() loop by giving it an exited child process
      if (fork() == 0) {
        std::exit(0);
      }
      // will break the for-loop as running is now false
      continue;
    }
    if (message->command == ParentToChildMessageType::kHostSuspend) {
      CF_EXPECT(SuspendHostProcessesImpl());
      continue;
    }
    if (message->command == ParentToChildMessageType::kHostResume) {
      CF_EXPECT(ResumeHostProcessesImpl());
      continue;
    }
  }
  return {};
}

Result<void> ProcessMonitor::SuspendHostProcessesImpl() {
  CF_EXPECT(child_channel_.has_value());
  CF_EXPECT(SuspendResumeImpl(properties_.entries_, properties_mutex_,
                              channel_to_secure_env_, /* is_suspend */ true,
                              *child_channel_),
            "Failed suspend");
  return {};
}

Result<void> ProcessMonitor::ResumeHostProcessesImpl() {
  CF_EXPECT(child_channel_.has_value());
  CF_EXPECT(SuspendResumeImpl(properties_.entries_, properties_mutex_,
                              channel_to_secure_env_, /* is_suspend */ false,
                              *child_channel_),
            "Failed resume");
  return {};
}

ProcessMonitor::Properties& ProcessMonitor::Properties::RestartSubprocesses(
    bool r) & {
  restart_subprocesses_ = r;
  return *this;
}

ProcessMonitor::Properties& ProcessMonitor::Properties::AddCommand(
    MonitorCommand cmd) & {
  entries_.emplace_back(std::move(cmd.command), cmd.is_critical);
  return *this;
}

ProcessMonitor::Properties& ProcessMonitor::Properties::StraceCommands(
    std::set<std::string> strace) & {
  strace_commands_ = std::move(strace);
  return *this;
}

ProcessMonitor::Properties& ProcessMonitor::Properties::StraceLogDir(
    std::string log_dir) & {
  strace_log_dir_ = std::move(log_dir);
  return *this;
}

ProcessMonitor::ProcessMonitor(ProcessMonitor::Properties&& properties,
                               const SharedFD& secure_env_fd)
    : properties_(std::move(properties)),
      channel_to_secure_env_(secure_env_fd),
      monitor_(-1) {}

Result<void> ProcessMonitor::StopMonitoredProcesses() {
  CF_EXPECT(monitor_ != -1, "The monitor process has already exited.");
  CF_EXPECT(parent_channel_.has_value(),
            "The monitor socket is already closed");
  CF_EXPECT(
      SendEmptyRequest(*parent_channel_, ParentToChildMessageType::kStop));

  pid_t last_monitor = monitor_;
  monitor_ = -1;
  parent_channel_.reset();
  int wstatus;
  CF_EXPECT(waitpid(last_monitor, &wstatus, 0) == last_monitor,
            "Failed to wait for monitor process");
  CF_EXPECT(!WIFSIGNALED(wstatus), "Monitor process exited due to a signal");
  CF_EXPECT(WIFEXITED(wstatus), "Monitor process exited for unknown reasons");
  CF_EXPECT(WEXITSTATUS(wstatus) == 0,
            "Monitor process exited with code " << WEXITSTATUS(wstatus));
  return {};
}

Result<void> ProcessMonitor::SuspendMonitoredProcesses() {
  CF_EXPECT(monitor_ != -1, "The monitor process has already exited.");
  CF_EXPECT(parent_channel_.has_value());
  CF_EXPECT(SendEmptyRequest(*parent_channel_,
                             ParentToChildMessageType::kHostSuspend));

  ManagedMessage response = CF_EXPECT(parent_channel_->ReceiveMessage());
  CF_EXPECT(response->command == ChildToParentResponseType::kSuccess,
            "On kHostSuspend, the child run_cvd returned kFailure.");
  return {};
}

Result<void> ProcessMonitor::ResumeMonitoredProcesses() {
  CF_EXPECT(monitor_ != -1, "The monitor process has already exited.");
  CF_EXPECT(parent_channel_.has_value());
  CF_EXPECT(SendEmptyRequest(*parent_channel_,
                             ParentToChildMessageType::kHostResume));

  ManagedMessage response = CF_EXPECT(parent_channel_->ReceiveMessage());
  CF_EXPECT(response->command == ChildToParentResponseType::kSuccess,
            "On kHostResume, the child run_cvd returned kFailure.");
  return {};
}

Result<void> ProcessMonitor::StartAndMonitorProcesses() {
  CF_EXPECT(monitor_ == -1, "The monitor process was already started");
  CF_EXPECT(!parent_channel_.has_value(),
            "Parent monitor socket was already opened");
  SharedFD pipe_read, pipe_write;
  CF_EXPECT(SharedFD::Pipe(&pipe_read, &pipe_write));

  SharedFD parent_sock;
  SharedFD child_sock;
  SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0, &parent_sock, &child_sock);
  monitor_ = fork();
  if (monitor_ == 0) {
    pipe_read->Close();
    child_channel_ = transport::SharedFdChannel(child_sock, child_sock);
    Result<void> monitor_result = MonitorRoutine();
    if (!monitor_result.ok()) {
      LOG(ERROR) << "Monitoring processes failed:\n" << monitor_result.error();
    }
    pipe_write->Close();
    std::exit(monitor_result.ok() ? 0 : 1);
  } else {
    pipe_write->Close();
    parent_channel_ = transport::SharedFdChannel(parent_sock, parent_sock);
    status_ = pipe_read;
    return {};
  }
}

Result<void> ProcessMonitor::MonitorRoutine() {
#ifdef __linux__
  // Make this process a subreaper to reliably catch subprocess exits.
  // See https://man7.org/linux/man-pages/man2/prctl.2.html
  prctl(PR_SET_CHILD_SUBREAPER, 1);
  prctl(PR_SET_PDEATHSIG, SIGHUP);  // Die when parent dies
#endif

  VLOG(0) << "Monitoring subprocesses";
  CF_EXPECT(StartSubprocesses(properties_));

  std::atomic_bool running(true);

  auto read_monitor_socket_loop =
      [this](std::atomic_bool& running) -> Result<void> {
    CF_EXPECT(this->ReadMonitorSocketLoop(running));
    return {};
  };
  auto parent_comms = std::async(std::launch::async, read_monitor_socket_loop,
                                 std::ref(running));

  CF_EXPECT(MonitorLoop(running, properties_mutex_,
                        properties_.restart_subprocesses_,
                        properties_.entries_));
  CF_EXPECT(parent_comms.get(), "Should have exited if monitoring stopped");

  CF_EXPECT(StopSubprocesses(properties_.entries_));
  VLOG(0) << "Done monitoring subprocesses";
  return {};
}

}  // namespace cuttlefish
