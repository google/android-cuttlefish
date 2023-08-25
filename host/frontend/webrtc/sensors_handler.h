/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/frontend/webrtc/sensors_simulator.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cuttlefish {
namespace webrtc_streaming {

struct SensorsHandler {
  SensorsHandler();
  ~SensorsHandler();
  void HandleMessage(const double x, const double y, const double z);
  int Subscribe(std::function<void(const uint8_t*, size_t)> send_to_client);
  void UnSubscribe(int subscriber_id);

 private:
  void UpdateSensors();
  SensorsSimulator* sensors_simulator_ = new SensorsSimulator();
  std::unordered_map<int, std::function<void(const uint8_t*, size_t)>> client_channels_;
  int last_client_channel_id_ = -1;
  std::mutex subscribers_mtx_;
};
}  // namespace webrtc_streaming
}  // namespace cuttlefish
