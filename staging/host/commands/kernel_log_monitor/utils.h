/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <json/json.h>
#include <optional>

#include "common/libs/fs/shared_fd.h"
#include "host/commands/kernel_log_monitor/kernel_log_server.h"

namespace cuttlefish::monitor {

struct ReadEventResult {
  Event event;
  Json::Value metadata;
};

// Read a kernel log event from fd.
std::optional<ReadEventResult> ReadEvent(SharedFD fd);

// Writes a kernel log event to the fd, in a format expected by ReadEvent.
bool WriteEvent(SharedFD fd, const Json::Value& event_message);

}  // namespace cuttlefish::monitor
