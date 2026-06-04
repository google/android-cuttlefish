/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <memory>
#include <string>

#include "absl/time/time.h"

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// TracingSession is a thin wrapper around Perfetto that enables recording
// tracing events in Cuttlefish processes which frequently fork.
//
// TracingSession spawns a forwarding server and performs the necessary
// environment configuration so that subsequent CF_TRACE macros calls
// in the current process and any forked children can connect to it.
class TracingSession {
 public:
  // Creates a new session and blocks until the Perfetto daemon has
  // connected and is accepting Cuttlefish trace events.
  static Result<std::unique_ptr<TracingSession>> StartBlocking(
      absl::Duration timeout);

  ~TracingSession();

  TracingSession(TracingSession&&) = delete;
  TracingSession& operator=(TracingSession&&) = delete;

  TracingSession(const TracingSession&) = delete;
  TracingSession& operator=(const TracingSession&) = delete;

 private:
  std::string socket_path_;
  Subprocess server_subprocess_;

  explicit TracingSession(std::string socket_path,
                          Subprocess server_subprocess);
};

}  // namespace cuttlefish