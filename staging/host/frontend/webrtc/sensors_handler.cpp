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

namespace {
static constexpr sensors::SensorsMask kUiSupportedSensors =
    (1 << sensors::kAccelerationId) | (1 << sensors::kGyroscopeId) |
    (1 << sensors::kMagneticId) | (1 << sensors::kRotationVecId);
}  // namespace

SensorsHandler::SensorsHandler(SharedFD sensors_fd)
    : channel_(transport::SharedFdChannel(sensors_fd, sensors_fd)) {
  auto refresh_result = RefreshSensors(0, 0, 0);
  if (!refresh_result.ok()) {
    LOG(ERROR) << "Failed to refresh sensors: "
               << refresh_result.error().FormatForEnv();
  }
}

SensorsHandler::~SensorsHandler() {}

Result<void> SensorsHandler::RefreshSensors(const double x, const double y,
                                            const double z) {
  std::stringstream ss;
  ss << x << sensors::INNER_DELIM << y << sensors::INNER_DELIM << z;
  auto msg = ss.str();
  auto size = msg.size();
  auto cmd = sensors::kUpdateRotationVec;
  auto request = CF_EXPECT(transport::CreateMessage(cmd, size),
                           "Failed to allocate message for cmd: "
                               << cmd << " with size: " << size << " bytes. ");
  std::memcpy(request->payload, msg.data(), size);
  CF_EXPECT(channel_.SendRequest(*request),
            "Can't send request for cmd: " << cmd);
  return {};
}

Result<std::string> SensorsHandler::GetSensorsData() {
  auto msg = std::to_string(kUiSupportedSensors);
  auto size = msg.size();
  auto cmd = sensors::kGetSensorsData;
  auto request = CF_EXPECT(transport::CreateMessage(cmd, size),
                           "Failed to allocate message for cmd: "
                               << cmd << " with size: " << size << " bytes. ");
  std::memcpy(request->payload, msg.data(), size);
  CF_EXPECT(channel_.SendRequest(*request),
            "Can't send request for cmd: " << cmd);
  auto response =
      CF_EXPECT(channel_.ReceiveMessage(), "Couldn't receive message.");
  cmd = response->command;
  auto is_response = response->is_response;
  CF_EXPECT((cmd == sensors::kGetSensorsData) && is_response,
            "Unexpected cmd: " << cmd << ", response: " << is_response);
  return std::string(reinterpret_cast<const char*>(response->payload),
                     response->payload_size);
}

// Get new sensor values and send them to client.
void SensorsHandler::HandleMessage(const double x, const double y, const double z) {
  auto refresh_result = RefreshSensors(x, y, z);
  if (!refresh_result.ok()) {
    LOG(ERROR) << "Failed to refresh sensors: "
               << refresh_result.error().FormatForEnv();
    return;
  }
  UpdateSensorsUi();
}

int SensorsHandler::Subscribe(std::function<void(const uint8_t*, size_t)> send_to_client) {
  int subscriber_id = ++last_client_channel_id_;
  {
    std::lock_guard<std::mutex> lock(subscribers_mtx_);
    client_channels_[subscriber_id] = send_to_client;
  }

  // Send device's initial state to the new client.
  auto result = GetSensorsData();
  if (!result.ok()) {
    LOG(ERROR) << "Failed to get sensors data: "
               << result.error().FormatForEnv();
    return subscriber_id;
  }
  auto new_sensors_data = std::move(result.value());
  const uint8_t* message =
      reinterpret_cast<const uint8_t*>(new_sensors_data.c_str());
  send_to_client(message, new_sensors_data.size());

  return subscriber_id;
}

void SensorsHandler::UnSubscribe(int subscriber_id) {
  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  client_channels_.erase(subscriber_id);
}

void SensorsHandler::UpdateSensorsUi() {
  auto result = GetSensorsData();
  if (!result.ok()) {
    LOG(ERROR) << "Failed to get sensors data: "
               << result.error().FormatForEnv();
    return;
  }
  auto new_sensors_data = std::move(result.value());
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
