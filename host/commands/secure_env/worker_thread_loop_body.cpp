//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/secure_env/worker_thread_loop_body.h"

#include <android-base/scopeguard.h>

#include "common/libs/fs/shared_select.h"

namespace cuttlefish {
namespace secure_env_impl {

void WorkerInnerLoop(std::function<bool()> process_callback,
                     SnapshotRunningFlag& running, SharedFD read_fd,
                     SharedFD suspend_event_fd,
                     EventNotifier& suspended_notifier) {
  bool break_loop = false;
  do {
    SharedFDSet event_and_read_fds;
    event_and_read_fds.Set(read_fd);
    event_and_read_fds.Set(suspend_event_fd);

    // blocking wait for running_ == true
    running.WaitRunning();
    int num_fds = Select(&event_and_read_fds, nullptr, nullptr, nullptr);
    if (num_fds < 0) {
      LOG(FATAL) << "Select() returned a negative value: " << num_fds
                 << strerror(errno);
    }

    // will run the lambda function in its destructor
    android::base::ScopeGuard inner_loop_body_exit_action(
        [&event_and_read_fds, &suspend_event_fd, &suspended_notifier]() {
          if (event_and_read_fds.IsSet(suspend_event_fd)) {
            eventfd_t value;
            if (suspend_event_fd->EventfdRead(&value) != 0) {
              LOG(FATAL) << "Eventfd was set but failed to be read."
                         << suspend_event_fd->StrError();
            }
            suspended_notifier.Notify();
          }
        });
    if (event_and_read_fds.IsSet(read_fd)) {
      // if process_callback() fails, we need to reset the secure_env
      // component.
      break_loop = !process_callback();
    }
  } while (!break_loop);
}

}  // namespace secure_env_impl
}  // namespace cuttlefish
