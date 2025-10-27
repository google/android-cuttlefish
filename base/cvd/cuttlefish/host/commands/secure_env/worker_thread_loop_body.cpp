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

#include "cuttlefish/host/commands/secure_env/worker_thread_loop_body.h"

#include "cuttlefish/common/libs/fs/shared_select.h"
#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/host/commands/secure_env/suspend_resume_handler.h"

namespace cuttlefish {
namespace secure_env_impl {

Result<void> WorkerInnerLoop(std::function<bool()> process_callback,
                             SharedFD read_fd, SharedFD snapshot_socket) {
  for (;;) {
    SharedFDSet readable_fds;
    readable_fds.Set(read_fd);
    readable_fds.Set(snapshot_socket);

    int num_fds = Select(&readable_fds, nullptr, nullptr, nullptr);
    if (num_fds < 0) {
      LOG(FATAL) << "Select() returned a negative value: " << num_fds
                 << StrError(errno);
    }

    if (readable_fds.IsSet(read_fd)) {
      // if process_callback() fails, we need to reset the secure_env
      // component.
      if (!process_callback()) {
        // NOTE: We don't need to worry about whether `snapshot_socket` is
        // readable at this point. After the component is reset, we'll re-enter
        // this loop and take care of it.
        break;
      }
      continue;
    }

    if (readable_fds.IsSet(snapshot_socket)) {
      // Read the suspend request.
      SnapshotSocketMessage suspend_request;
      CF_EXPECT_EQ(
          sizeof(suspend_request),
          snapshot_socket->Read(&suspend_request, sizeof(suspend_request)),
          "socket read failed: " << snapshot_socket->StrError());
      CF_EXPECT_EQ(SnapshotSocketMessage::kSuspend, suspend_request);
      // Send the ACK response.
      const SnapshotSocketMessage ack_response =
          SnapshotSocketMessage::kSuspendAck;
      CF_EXPECT_EQ(sizeof(ack_response),
                   snapshot_socket->Write(&ack_response, sizeof(ack_response)),
                   "socket write failed: " << snapshot_socket->StrError());
      // Block until resumed.
      SnapshotSocketMessage resume_request;
      CF_EXPECT_EQ(
          sizeof(resume_request),
          snapshot_socket->Read(&resume_request, sizeof(resume_request)),
          "socket read failed: " << snapshot_socket->StrError());
      CF_EXPECT_EQ(SnapshotSocketMessage::kResume, resume_request);
    }
  }

  return {};
}

}  // namespace secure_env_impl
}  // namespace cuttlefish
