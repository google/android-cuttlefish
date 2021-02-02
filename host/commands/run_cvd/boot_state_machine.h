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
#pragma once

#include <memory>
#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "host/commands/run_cvd/runner_defs.h"

namespace cuttlefish {

// Maintains the state of the boot process, once a final state is reached
// (success or failure) it sends the appropriate exit code to the foreground
// launcher process
class CvdBootStateMachine {
 public:
  CvdBootStateMachine(SharedFD fg_launcher_pipe, SharedFD reboot_notification,
                      SharedFD boot_events_pipe);
  ~CvdBootStateMachine();

 private:
  // Returns true if the machine is left in a final state
  bool OnBootEvtReceived(SharedFD boot_events_pipe);
  bool BootCompleted() const;
  bool BootFailed() const;

  void SendExitCode(RunnerExitCodes exit_code, SharedFD fd);
  bool MaybeWriteNotification();

  std::thread boot_event_handler_;
  SharedFD fg_launcher_pipe_;
  SharedFD reboot_notification_;
  int state_;
  static const int kBootStarted = 0;
  static const int kGuestBootCompleted = 1 << 0;
  static const int kGuestBootFailed = 1 << 1;
};

}  // namespace cuttlefish
