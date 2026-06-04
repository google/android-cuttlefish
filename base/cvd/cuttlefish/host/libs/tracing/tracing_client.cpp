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

#include "cuttlefish/host/libs/tracing/tracing_client.h"

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/tracing/tracing.pb.h"
#include "cuttlefish/host/libs/tracing/tracing_utils.h"

namespace cuttlefish {

std::unique_ptr<TracingClient> TracingClient::Create() {
  auto path = StringFromEnv(kTracingSocketPathEnv);
  if (!path.has_value() || path->empty()) {
    return nullptr;
  }
  auto fd = SharedFD::SocketLocalClient(*path, false, SOCK_DGRAM);
  if (!fd->IsOpen()) {
    LOG(ERROR) << "Failed to connect to tracing socket at " << *path << ": "
               << fd->StrError();
    return nullptr;
  }
  return std::unique_ptr<TracingClient>(new TracingClient(std::move(fd)));
}

TracingClient::TracingClient(SharedFD socket) : socket_(std::move(socket)) {}

void TracingClient::SendEventProto(const TraceEventProto& proto) {
  std::string buffer;
  if (!proto.SerializeToString(&buffer)) {
    LOG(ERROR) << "Failed to serialize TraceEventProto";
    return;
  }

  if (buffer.size() > kMaxTracePacketSize) {
    LOG(ERROR) << "Failed to send TraceEventProto: too large";
    return;
  }

  auto bytes_written = socket_->Write(buffer.data(), buffer.size());
  if (bytes_written != buffer.size()) {
    LOG(ERROR) << "Failed to write TraceEventProto to socket: "
               << socket_->StrError();
  }
}

}  // namespace cuttlefish
