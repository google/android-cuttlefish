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

  auto local_client = SharedFD::SocketLocalClient(port, SOCK_STREAM);
  CHECK(local_client->IsOpen()) << "Failed to connect to adb socket: " << local_client->StrError();
  return local_client;
}

}  // namespace

AdbHandler::AdbHandler(
    const std::string &adb_host_and_port,
    std::function<void(const uint8_t *, size_t)> send_to_client)
    : send_to_client_(send_to_client),
      adb_socket_(SetupAdbSocket(adb_host_and_port)),
      shutdown_(SharedFD::Event(0,0))
{
    std::thread loop([this]() { ReadLoop(); });
    read_thread_.swap(loop);
}


AdbHandler::~AdbHandler() {
    // Send a message to the looper to shut down.
    uint64_t v = 1;
    shutdown_->Write(&v, sizeof(v));
    // Shut down the socket as well.  Not srictly necessary.
    adb_socket_->Shutdown(SHUT_RDWR);
    read_thread_.join();
}

void AdbHandler::ReadLoop() {
  while (1) {
    uint8_t buffer[4096];

    read_set_.Set(shutdown_);
    read_set_.Set(adb_socket_);
    Select(&read_set_, nullptr, nullptr, nullptr);

    if (read_set_.IsSet(adb_socket_)) {
        auto read = adb_socket_->Read(buffer, sizeof(buffer));
        if (read < 0) {
            LOG(ERROR) << "Error on reading from ADB socket: " << strerror(adb_socket_->GetErrno());
            break;
        }
        if (read) {
            send_to_client_(buffer, read);
        }
    }

    if (read_set_.IsSet(shutdown_)) {
        LOG(INFO) << "AdbHandler is shutting down.";
        break;
    }
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
