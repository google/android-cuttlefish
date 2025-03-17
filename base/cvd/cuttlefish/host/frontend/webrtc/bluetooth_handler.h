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

#pragma once

#include <memory>
#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

namespace cuttlefish {
namespace webrtc_streaming {

struct BluetoothHandler {
  explicit BluetoothHandler(
      const int rootCanalTestPort,
      std::function<void(const uint8_t *, size_t)> send_to_client);

  ~BluetoothHandler();

  void handleMessage(const uint8_t *msg, size_t len);

 private:
  std::function<void(const uint8_t *, size_t)> send_to_client_;

  void ReadLoop();

  SharedFD rootcanal_socket_;
  SharedFD shutdown_;
  SharedFDSet read_set_;
  std::thread read_thread_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
