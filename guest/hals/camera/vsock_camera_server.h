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
#include <atomic>
#include <thread>
#include "vsock_camera_device_3_4.h"
#include "vsock_connection.h"

namespace android::hardware::camera::provider::V2_7::implementation {

using ::android::hardware::camera::device::V3_4::implementation::
    VsockCameraDevice;

class VsockCameraServer {
 public:
  VsockCameraServer();
  ~VsockCameraServer();

  VsockCameraServer(const VsockCameraServer&) = delete;
  VsockCameraServer& operator=(const VsockCameraServer&) = delete;

  bool isRunning() const { return is_running_.load(); }

  void start(unsigned int port, unsigned int cid);
  void stop();

  using callback_t =
      std::function<void(std::shared_ptr<cuttlefish::VsockConnection>,
                         VsockCameraDevice::Settings)>;
  void setConnectedCallback(callback_t callback);

 private:
  void serverLoop(unsigned int port, unsigned int cid);
  std::thread server_thread_;
  std::atomic<bool> is_running_;
  std::shared_ptr<cuttlefish::VsockServerConnection> connection_;
  std::mutex settings_mutex_;
  VsockCameraDevice::Settings settings_;
  callback_t connected_callback_;
};

}  // namespace android::hardware::camera::provider::V2_7::implementation
