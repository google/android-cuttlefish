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

#include <string>
#include <unordered_map>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

/*
 * Event FDs that Suspend handler triggers, and each worker thread
 * is assigned and monitors.
 *
 * Each worker thread is assigned an event fd created in advance.
 * Thus, we don't know the thread ID. We use a human-readable name
 * and mapping from the thread name to the event fd.
 */
class EventFdsManager {
 public:
  static Result<EventFdsManager> Create();
  EventFdsManager(EventFdsManager&&) = default;
  EventFdsManager(const EventFdsManager&) = delete;

  SharedFD KeymasterEventFd();
  SharedFD GatekeeperEventFd();
  SharedFD OemlockEventFd();

  Result<void> SuspendKeymasterResponder();
  Result<void> SuspendGatekeeperResponder();
  Result<void> SuspendOemlockResponder();

 private:
  struct EventFdsManagerParam {
    SharedFD keymaster_event_fd_;
    SharedFD gatekeeper_event_fd_;
    SharedFD oemlock_event_fd_;
  };
  EventFdsManager(const EventFdsManagerParam& param);

  SharedFD keymaster_event_fd_;
  SharedFD gatekeeper_event_fd_;
  SharedFD oemlock_event_fd_;
};

}  // namespace cuttlefish
