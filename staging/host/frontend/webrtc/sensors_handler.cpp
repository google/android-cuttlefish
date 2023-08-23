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

SensorsHandler::~SensorsHandler() {
  // Send a message to the looper to shut down.
  uint64_t v = 1;
  shutdown_->Write(&v, sizeof(v));
}

void SensorsHandler::InitializeHandler(std::function<void(const uint8_t*, size_t)> send_to_client) {
  send_to_client_ = send_to_client;
}

// Send device's initial rotation angles and sensor values to display.
void SensorsHandler::SendInitialState() {
  std::string new_sensors_data = sensors_simulator_->GetSensorsData();
  const uint8_t* message =
      reinterpret_cast<const uint8_t*>(new_sensors_data.c_str());
  send_to_client_(message, new_sensors_data.size());
}

// Get new sensor values and send them to client.
void SensorsHandler::HandleMessage(const double x, const double y, const double z) {
  sensors_simulator_->RefreshSensors(x, y, z);
  std::string new_sensors_data = sensors_simulator_->GetSensorsData();
  const uint8_t* message =
      reinterpret_cast<const uint8_t*>(new_sensors_data.c_str());
  send_to_client_(message, new_sensors_data.size());
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
