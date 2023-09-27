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

#include "host/frontend/webrtc/sensors_handler.h"

#include <android-base/logging.h>

#include <sstream>
#include <string>

namespace cuttlefish {
namespace webrtc_streaming {

SensorsHandler::SensorsHandler() {}

SensorsHandler::~SensorsHandler() {}

// Get new sensor values and send them to client.
void SensorsHandler::HandleMessage(const double x, const double y, const double z) {
  sensors_simulator_->RefreshSensors(x, y, z);
  UpdateSensors();
}

int SensorsHandler::Subscribe(std::function<void(const uint8_t*, size_t)> send_to_client) {
  int subscriber_id = ++last_client_channel_id_;
  {
    std::lock_guard<std::mutex> lock(subscribers_mtx_);
    client_channels_[subscriber_id] = send_to_client;
  }

  // Send device's initial state to the new client.
  std::string new_sensors_data = sensors_simulator_->GetSensorsData();
  const uint8_t* message =
      reinterpret_cast<const uint8_t*>(new_sensors_data.c_str());
  send_to_client(message, new_sensors_data.size());

  return subscriber_id;
}

void SensorsHandler::UnSubscribe(int subscriber_id) {
  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  client_channels_.erase(subscriber_id);
}

void SensorsHandler::UpdateSensors() {
  std::string new_sensors_data = sensors_simulator_->GetSensorsData();
  const uint8_t* message =
      reinterpret_cast<const uint8_t*>(new_sensors_data.c_str());
  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  for (auto itr = client_channels_.begin(); itr != client_channels_.end();
       itr++) {
    itr->second(message, new_sensors_data.size());
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
