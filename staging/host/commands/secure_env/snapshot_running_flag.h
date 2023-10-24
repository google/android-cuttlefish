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

#include <condition_variable>
#include <mutex>

namespace cuttlefish {

class SnapshotRunningFlag {
 public:
  SnapshotRunningFlag() {}
  // called by Suspend handler
  void UnsetRunning();

  // called by Resume handler
  void SetRunning();

  // called by each worker thread
  // blocks if running_ is false, and wakes up on running_ == true
  void WaitRunning();

 private:
  bool running_ = true;
  std::mutex running_mutex_;
  std::condition_variable running_true_cv_;
};

}  // namespace cuttlefish
