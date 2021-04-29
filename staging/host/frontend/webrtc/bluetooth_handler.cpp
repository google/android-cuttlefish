/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/frontend/webrtc/bluetooth_handler.h"

#include <unistd.h>

#include <android-base/logging.h>

using namespace android;

namespace cuttlefish {
namespace webrtc_streaming {

BluetoothHandler::BluetoothHandler(
    const int rootCanalTestPort,
    std::function<void(const uint8_t *, size_t)> send_to_client)
    : send_to_client_(send_to_client),
      rootcanal_socket_(
          SharedFD::SocketLocalClient(rootCanalTestPort, SOCK_STREAM)),
      shutdown_(SharedFD::Event(0, 0)) {
  std::thread loop([this]() { ReadLoop(); });
  read_thread_.swap(loop);
}

BluetoothHandler::~BluetoothHandler() {
  // Send a message to the looper to shut down.
  uint64_t v = 1;
  shutdown_->Write(&v, sizeof(v));
  // Shut down the socket as well.  Not strictly necessary.
  rootcanal_socket_->Shutdown(SHUT_RDWR);
  read_thread_.join();
}

void BluetoothHandler::ReadLoop() {
  while (1) {
    uint8_t buffer[4096];

    read_set_.Set(shutdown_);
    read_set_.Set(rootcanal_socket_);
    Select(&read_set_, nullptr, nullptr, nullptr);

    if (read_set_.IsSet(rootcanal_socket_)) {
      auto read = rootcanal_socket_->Read(buffer, sizeof(buffer));
      if (read < 0) {
        PLOG(ERROR) << "Error on reading from RootCanal socket.";
        break;
      }
      if (read) {
        send_to_client_(buffer, read);
      }
    }

    if (read_set_.IsSet(shutdown_)) {
      LOG(INFO) << "BluetoothHandler is shutting down.";
      break;
    }
  }
}

void BluetoothHandler::handleMessage(const uint8_t *msg, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    auto this_sent = rootcanal_socket_->Write(&msg[sent], len - sent);
    if (this_sent < 0) {
      PLOG(FATAL) << "Error writing to rootcanal socket.";
      return;
    }
    sent += this_sent;
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
