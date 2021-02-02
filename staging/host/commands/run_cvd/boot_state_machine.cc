/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/commands/run_cvd/boot_state_machine.h"

#include <memory>
#include <thread>

#include "android-base/logging.h"
#include "common/libs/fs/shared_fd.h"
#include "host/commands/kernel_log_monitor/kernel_log_server.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/commands/run_cvd/runner_defs.h"

namespace cuttlefish {

CvdBootStateMachine::CvdBootStateMachine(SharedFD fg_launcher_pipe,
                                         SharedFD reboot_notification,
                                         SharedFD boot_events_pipe)
    : fg_launcher_pipe_(fg_launcher_pipe),
      reboot_notification_(reboot_notification),
      state_(kBootStarted) {
  boot_event_handler_ = std::thread([this, boot_events_pipe]() {
    while (true) {
      SharedFDSet fd_set;
      fd_set.Set(boot_events_pipe);
      int result = Select(&fd_set, nullptr, nullptr, nullptr);
      if (result < 0) {
        PLOG(FATAL) << "Failed to call Select";
        return;
      }
      if (!fd_set.IsSet(boot_events_pipe)) {
        continue;
      }
      auto sent_code = OnBootEvtReceived(boot_events_pipe);
      if (sent_code) {
        break;
      }
    }
  });
}

CvdBootStateMachine::~CvdBootStateMachine() { boot_event_handler_.join(); }

// Returns true if the machine is left in a final state
bool CvdBootStateMachine::OnBootEvtReceived(SharedFD boot_events_pipe) {
  std::optional<monitor::ReadEventResult> read_result =
      monitor::ReadEvent(boot_events_pipe);
  if (!read_result) {
    LOG(ERROR) << "Failed to read a complete kernel log boot event.";
    state_ |= kGuestBootFailed;
    return MaybeWriteNotification();
  }

  if (read_result->event == monitor::Event::BootCompleted) {
    LOG(INFO) << "Virtual device booted successfully";
    state_ |= kGuestBootCompleted;
  } else if (read_result->event == monitor::Event::BootFailed) {
    LOG(ERROR) << "Virtual device failed to boot";
    state_ |= kGuestBootFailed;
  }  // Ignore the other signals

  return MaybeWriteNotification();
}

bool CvdBootStateMachine::BootCompleted() const {
  return state_ & kGuestBootCompleted;
}

bool CvdBootStateMachine::BootFailed() const {
  return state_ & kGuestBootFailed;
}

void CvdBootStateMachine::SendExitCode(RunnerExitCodes exit_code, SharedFD fd) {
  fd->Write(&exit_code, sizeof(exit_code));
  // The foreground process will exit after receiving the exit code, if we try
  // to write again we'll get a SIGPIPE
  fd->Close();
}

bool CvdBootStateMachine::MaybeWriteNotification() {
  std::vector<SharedFD> fds = {reboot_notification_, fg_launcher_pipe_};
  for (auto& fd : fds) {
    if (fd->IsOpen()) {
      if (BootCompleted()) {
        SendExitCode(RunnerExitCodes::kSuccess, fd);
      } else if (state_ & kGuestBootFailed) {
        SendExitCode(RunnerExitCodes::kVirtualDeviceBootFailed, fd);
      }
    }
  }
  // Either we sent the code before or just sent it, in any case the state is
  // final
  return BootCompleted() || (state_ & kGuestBootFailed);
}

}  // namespace cuttlefish
