/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "host/frontend/webrtc/adb_handler.h"

#include <unistd.h>

#include <android-base/logging.h>

using namespace android;

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

SharedFD SetupAdbSocket(const std::string &adb_host_and_port) {
  auto colonPos = adb_host_and_port.find(':');
  if (colonPos == std::string::npos) {
    return SharedFD();
  }

  auto host = adb_host_and_port.substr(0, colonPos);

  const char *portString = adb_host_and_port.c_str() + colonPos + 1;
  char *end;
  unsigned long port = strtoul(portString, &end, 10);

  if (end == portString || *end != '\0' || port > 65535) {
    return SharedFD();
  }

  return SharedFD::SocketLocalClient(port, SOCK_STREAM);
}

}  // namespace

AdbHandler::AdbHandler(
    const std::string &adb_host_and_port,
    std::function<void(const uint8_t *, size_t)> send_to_client)
    : send_to_client_(send_to_client),
      adb_socket_(SetupAdbSocket(adb_host_and_port)),
      read_thread_([this]() { ReadLoop(); }) {}

AdbHandler::~AdbHandler() = default;

void AdbHandler::ReadLoop() {
  while (1) {
    uint8_t buffer[4096];
    auto read = adb_socket_->Read(buffer, sizeof(buffer));
    send_to_client_(buffer, read);
  }
}

void AdbHandler::handleMessage(const uint8_t *msg, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    auto this_sent = adb_socket_->Write(&msg[sent], len - sent);
    if (this_sent < 0) {
      LOG(FATAL) << "Error writing to adb socket: " << adb_socket_->StrError();
      return;
    }
    sent += this_sent;
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
