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

#include <atomic>
#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/command_util/runner/defs.h"

namespace cuttlefish {

class SnapshotCommandHandler {
 public:
  ~SnapshotCommandHandler();
  SnapshotCommandHandler(SharedFD channel_to_run_cvd,
                         std::atomic<bool>& running);

 private:
  Result<void> SuspendResumeHandler();
  Result<ExtendedActionType> ReadRunCvdSnapshotCmd() const;
  void Join();

  SharedFD channel_to_run_cvd_;
  std::atomic<bool>& shared_running_;  // shared by other components outside
  std::thread handler_thread_;
};

}  // namespace cuttlefish
