/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "host/commands/sensors_simulator/sensors_hal_proxy.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace sensors {

namespace {
static constexpr char END_OF_MSG = '\n';
static constexpr uint32_t kIntervalMs = 1000;

/*
  Aligned with Goldfish sensor flags defined in
  `device/generic/goldfish/hals/sensors/sensor_list.cpp`.
*/
static constexpr SensorsMask kContinuousModeSensors =
    (1 << kAccelerationId) | (1 << kGyroscopeId) | (1 << kMagneticId) |
    (1 << kPressureId) | (1 << kUncalibGyroscopeId) |
    (1 << kUncalibAccelerationId);

Result<std::string> SensorIdToName(int id) {
  switch (id) {
    case kAccelerationId:
      return "acceleration";
    case kGyroscopeId:
      return "gyroscope";
    case kMagneticId:
      return "magnetic";
    case kTemperatureId:
      return "temperature";
    case kProximityId:
      return "proximity";
    case kLightId:
      return "light";
    case kPressureId:
      return "pressure";
    case kHumidityId:
      return "humidity";
    case kUncalibMagneticId:
      return "magnetic-uncalibrated";
    case kUncalibGyroscopeId:
      return "gyroscope-uncalibrated";
    case kHingeAngle0Id:
      return "hinge-angle0";
    case kUncalibAccelerationId:
      return "acceleration-uncalibrated";
    case kRotationVecId:
      return "rotation";
    default:
      return CF_ERR("Unsupported sensor id: " << id);
  }
}

Result<void> SendResponseHelper(transport::SharedFdChannel& channel,
                                const std::string& msg) {
  auto size = msg.size();
  auto cmd = sensors::kUpdateHal;
  auto response = CF_EXPECT(transport::CreateMessage(cmd, msg.size()),
                            "Failed to allocate message.");
  std::memcpy(response->payload, msg.data(), size);
  CF_EXPECT(channel.SendResponse(*response), "Can't update sensor HAL.");
  return {};
}

Result<void> ProcessHalRequest(transport::SharedFdChannel& channel,
                               std::atomic<bool>& hal_activated,
                               uint32_t mask) {
  auto request =
      CF_EXPECT(channel.ReceiveMessage(), "Couldn't receive message.");
  std::string payload(reinterpret_cast<const char*>(request->payload),
                      request->payload_size);
  if (payload.rfind("list-sensors", 0) == 0) {
    auto msg = std::to_string(mask) + END_OF_MSG;
    CF_EXPECT(SendResponseHelper(channel, msg));
    hal_activated = true;
  }
  return {};
}

Result<void> UpdateSensorsHal(const std::string& sensors_data,
                              transport::SharedFdChannel& channel,
                              uint32_t mask) {
  std::vector<std::string> reports;
  std::string report;
  std::stringstream sensors_data_stream(sensors_data);
  int id = 0;

  while (mask) {
    if (mask & 1) {
      CF_EXPECT(static_cast<bool>(sensors_data_stream >> report));
      auto result = SensorIdToName(id);
      if (result.ok()) {
        reports.push_back(result.value() + INNER_DELIM + report + END_OF_MSG);
      }
    }
    id += 1;
    mask >>= 1;
  }
  for (const auto& r : reports) {
    CF_EXPECT(SendResponseHelper(channel, r));
  }
  return {};
}

}  // namespace

SensorsHalProxy::SensorsHalProxy(SharedFD control_from_guest_fd,
                                 SharedFD control_to_guest_fd,
                                 SharedFD data_from_guest_fd,
                                 SharedFD data_to_guest_fd,
                                 SharedFD kernel_events_fd,
                                 SensorsSimulator& sensors_simulator,
                                 DeviceType device_type)
    : control_channel_(std::move(control_from_guest_fd),
                       std::move(control_to_guest_fd)),
      data_channel_(std::move(data_from_guest_fd), std::move(data_to_guest_fd)),
      kernel_events_fd_(std::move(kernel_events_fd)),
      sensors_simulator_(sensors_simulator) {
  SensorsMask host_enabled_sensors;
  switch (device_type) {
    case DeviceType::Foldable:
      host_enabled_sensors =
          (1 << kAccelerationId) | (1 << kGyroscopeId) | (1 << kMagneticId) |
          (1 << kTemperatureId) | (1 << kProximityId) | (1 << kLightId) |
          (1 << kPressureId) | (1 << kHumidityId) | (1 << kHingeAngle0Id);
      break;
    case DeviceType::Auto:
      host_enabled_sensors = (1 << kAccelerationId) | (1 << kGyroscopeId) |
                             (1 << kUncalibGyroscopeId) |
                             (1 << kUncalibAccelerationId);
      break;
    default:
      host_enabled_sensors = (1 << kAccelerationId) | (1 << kGyroscopeId) |
                             (1 << kMagneticId) | (1 << kTemperatureId) |
                             (1 << kProximityId) | (1 << kLightId) |
                             (1 << kPressureId) | (1 << kHumidityId);
  }

  req_responder_thread_ = std::thread([this, host_enabled_sensors] {
    while (running_) {
      auto result = ProcessHalRequest(control_channel_, hal_activated_,
                                      host_enabled_sensors);
      if (!result.ok()) {
        running_ = false;
        LOG(ERROR) << result.error().FormatForEnv();
      }
    }
  });
  data_reporter_thread_ = std::thread([this, host_enabled_sensors] {
    while (running_) {
      if (hal_activated_) {
        SensorsMask host_update_sensors =
            host_enabled_sensors & kContinuousModeSensors;
        auto sensors_data =
            sensors_simulator_.GetSensorsData(host_update_sensors);
        auto result =
            UpdateSensorsHal(sensors_data, data_channel_, host_update_sensors);
        if (!result.ok()) {
          running_ = false;
          LOG(ERROR) << result.error().FormatForEnv();
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kIntervalMs));
    }
  });
  reboot_monitor_thread_ = std::thread([this] {
    while (kernel_events_fd_->IsOpen()) {
      auto read_result = monitor::ReadEvent(kernel_events_fd_);
      CHECK(read_result.ok()) << read_result.error().FormatForEnv();
      CHECK(read_result->has_value()) << "EOF in kernel log monitor";
      if ((*read_result)->event == monitor::Event::BootloaderLoaded) {
        hal_activated_ = false;
      }
    }
  });
}

}  // namespace sensors
}  // namespace cuttlefish