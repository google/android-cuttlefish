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

/* only for secure_env, will be replaced with a better implementation
 * This is used for 1-to-1 communication only.
 *
 */
class EventNotifier {
 public:
  EventNotifier() = default;
  /*
   * Always sleep if a thread calls this. Wakes up on notification.
   */
  void WaitAndReset();
  void Notify();

 private:
  std::mutex m_;
  std::condition_variable cv_;
  bool flag_ = false;
};

struct EventNotifiers {
  EventNotifier keymaster_suspended_;
  EventNotifier gatekeeper_suspended_;
  EventNotifier oemlock_suspended_;
};

}  // namespace cuttlefish
