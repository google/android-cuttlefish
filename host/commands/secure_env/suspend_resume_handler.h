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

#pragma once

#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/secure_env/event_fds_manager.h"
#include "host/commands/secure_env/event_notifier.h"
#include "host/commands/secure_env/snapshot_running_flag.h"
#include "host/libs/command_util/runner/defs.h"

namespace cuttlefish {

// `SnapshotCommandHandler` can request threads to suspend and resume using the
// following protocol. Each message on the socket is 1 byte.
//
// Suspend flow:
//
//   1. `SnapshotCommandHandler` writes `kSuspend` to the socket.
//   2. When the worker thread sees the socket is readable, it should assume the
//      incoming message is `kSuspend`, finish all non-blocking work, read the
//      `kSuspend` message, write a `kSuspendAck` message back into the socket,
//      and then, finally, block until it receives another message from the
//      socket (which will always be `kResume`).
//   3. `SnapshotCommandHandler` waits for the `kSuspendAck` to ensure the
//      worker thread is actually suspended and then proceeds.
//
// Resume flow:
//
//   1. The worker thread is already blocked waiting for a `kResume` from the
//      socket.
//   2. `SnapshotCommandHandler` sends a `kResume`.
//   3. The worker thread sees it and goes back to normal operation.
//
// WARNING: Keep in sync with the `SNAPSHOT_SOCKET_MESSAGE_*` constants in
// secure_env/rust/lib.rs.
enum SnapshotSocketMessage : uint8_t {
  kSuspend = 1,
  kSuspendAck = 2,
  kResume = 3,
};

class SnapshotCommandHandler {
 public:
  ~SnapshotCommandHandler();
  SnapshotCommandHandler(SharedFD channel_to_run_cvd,
                         EventFdsManager& event_fds,
                         EventNotifiers& suspended_notifiers,
                         SnapshotRunningFlag& running,
                         SharedFD rust_snapshot_socket);

 private:
  Result<void> SuspendResumeHandler();
  Result<ExtendedActionType> ReadRunCvdSnapshotCmd() const;
  void Join();

  SharedFD channel_to_run_cvd_;
  EventFdsManager& event_fds_manager_;
  EventNotifiers& suspended_notifiers_;
  SnapshotRunningFlag& shared_running_;  // shared by other components outside
  SharedFD rust_snapshot_socket_;
  std::thread handler_thread_;
};

}  // namespace cuttlefish
