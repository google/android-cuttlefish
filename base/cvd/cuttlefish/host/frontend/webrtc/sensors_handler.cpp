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

#include "cuttlefish/host/frontend/webrtc/sensors_handler.h"

#include <string.h>

#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/sensors/sensors.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {
static constexpr sensors::SensorsMask kMotionSensors =
    (1 << sensors::kAccelerationId) | (1 << sensors::kGyroscopeId) |
    (1 << sensors::kMagneticId) | (1 << sensors::kRotationVecId);
}  // namespace

SensorsHandler::SensorsHandler(SharedFD sensors_fd)
    : channel_(transport::SharedFdChannel(sensors_fd, sensors_fd)) {}

SensorsHandler::~SensorsHandler() {}

Result<void> SensorsHandler::SendCommand(uint32_t cmd,
                                         std::string_view payload) {
  size_t size = payload.size();
  transport::ManagedMessage request = CF_EXPECT(
      transport::CreateMessage(cmd, size),
      "Failed to allocate message for cmd: " << cmd << " with size: " << size
                                             << " bytes. ");
  memcpy(request->payload, payload.data(), size);
  CF_EXPECT(channel_.SendRequest(*request),
            "Can't send request for cmd: " << cmd);
  return {};
}

Result<void> SensorsHandler::SetMotion(const double x, const double y,
                                       const double z) {
  std::stringstream ss;
  ss << x << sensors::INNER_DELIM << y << sensors::INNER_DELIM << z;
  CF_EXPECT(SendCommand(sensors::kUpdateRotationVec, ss.str()));
  NotifyMotionUpdated();
  return {};
}

Result<void> SensorsHandler::SetHingeAngle(const double angle) {
  CF_EXPECT(SendCommand(sensors::kUpdateHingeAngle, std::to_string(angle)));
  return {};
}

Result<std::string> SensorsHandler::GetSensorsData(sensors::SensorsMask mask) {
  CF_EXPECT(SendCommand(sensors::kGetSensorsData, std::to_string(mask)));
  transport::ManagedMessage response =
      CF_EXPECT(channel_.ReceiveMessage(), "Couldn't receive message.");
  uint32_t cmd = response->command;
  bool is_response = response->is_response;
  CF_EXPECT((cmd == sensors::kGetSensorsData) && is_response,
            "Unexpected cmd: " << cmd << ", response: " << is_response);
  return std::string(reinterpret_cast<const char*>(response->payload),
                     response->payload_size);
}

int SensorsHandler::AddMotionUpdatedCallback(MotionUpdatedCallback callback) {
  int subscriber_id = ++last_client_channel_id_;
  {
    std::lock_guard<std::mutex> lock(subscribers_mtx_);
    client_channels_[subscriber_id] = callback;
  }

  // Send device's initial state to the new client.
  Result<std::string> result = GetSensorsData(kMotionSensors);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to get sensors data: " << result.error();
    return subscriber_id;
  }
  std::string new_sensors_data = std::move(result.value());
  const uint8_t* message =
      reinterpret_cast<const uint8_t*>(new_sensors_data.c_str());
  callback(message, new_sensors_data.size());

  return subscriber_id;
}

void SensorsHandler::RemoveMotionUpdatedCallback(int subscriber_id) {
  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  client_channels_.erase(subscriber_id);
}

void SensorsHandler::NotifyMotionUpdated() {
  Result<std::string> result = GetSensorsData(kMotionSensors);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to get sensors data: " << result.error();
    return;
  }
  std::string new_sensors_data = std::move(result.value());
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
