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

#include "host/commands/secure_env/event_notifier.h"

namespace cuttlefish {

void EventNotifier::WaitAndReset() {
  std::unique_lock lock(m_);
  while (!flag_) {
    cv_.wait(lock);
  }
  flag_ = false;
}

void EventNotifier::Notify() {
  std::lock_guard lock(m_);
  flag_ = true;
  cv_.notify_all();
}

}  // namespace cuttlefish
