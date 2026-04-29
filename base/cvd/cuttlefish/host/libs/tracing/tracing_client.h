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

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/tracing/tracing.pb.h"

namespace cuttlefish {

class TracingClient {
 public:
  static std::unique_ptr<TracingClient> Create();

  ~TracingClient() = default;

  TracingClient(const TracingClient&) = delete;
  TracingClient& operator=(const TracingClient&) = delete;

  void SendEventProto(const TraceEventProto& proto);

 private:
  TracingClient(SharedFD socket);

  const SharedFD socket_;
};

}  // namespace cuttlefish