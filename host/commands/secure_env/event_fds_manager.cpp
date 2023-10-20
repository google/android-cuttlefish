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

#include "host/commands/secure_env/event_fds_manager.h"

#include <utility>
#include <vector>

#include "common/libs/utils/contains.h"

namespace cuttlefish {

static Result<SharedFD> CreateEventFd() {
  auto event_fd = SharedFD::Event();
  CF_EXPECT(event_fd->IsOpen(), event_fd->StrError());
  return event_fd;
}

Result<EventFdsManager> EventFdsManager::Create() {
  std::unordered_map<std::string, SharedFD> event_fds;
  return EventFdsManager({
      .keymaster_event_fd_ = CF_EXPECT(CreateEventFd()),
      .gatekeeper_event_fd_ = CF_EXPECT(CreateEventFd()),
      .oemlock_event_fd_ = CF_EXPECT(CreateEventFd()),
  });
}

EventFdsManager::EventFdsManager(
    const EventFdsManager::EventFdsManagerParam& param)
    : keymaster_event_fd_(param.keymaster_event_fd_),
      gatekeeper_event_fd_(param.gatekeeper_event_fd_),
      oemlock_event_fd_(param.oemlock_event_fd_) {}

SharedFD EventFdsManager::KeymasterEventFd() { return keymaster_event_fd_; }

SharedFD EventFdsManager::GatekeeperEventFd() { return gatekeeper_event_fd_; }

SharedFD EventFdsManager::OemlockEventFd() { return oemlock_event_fd_; }

static Result<void> WriteOneToEventfd(SharedFD fd) {
  CF_EXPECT(fd->IsOpen(), fd->StrError());
  CF_EXPECT_EQ(fd->EventfdWrite(1), 0, fd->StrError());
  return {};
}

Result<void> EventFdsManager::SuspendKeymasterResponder() {
  return WriteOneToEventfd(keymaster_event_fd_);
}

Result<void> EventFdsManager::SuspendGatekeeperResponder() {
  return WriteOneToEventfd(gatekeeper_event_fd_);
}

Result<void> EventFdsManager::SuspendOemlockResponder() {
  return WriteOneToEventfd(oemlock_event_fd_);
}

}  // namespace cuttlefish
