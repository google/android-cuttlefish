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

#include <chrono>
#include <string>

#include <Eigen/Dense>

#include "common/libs/sensors/sensors.h"

namespace cuttlefish {
namespace sensors {

class SensorsSimulator {
 public:
  SensorsSimulator();
  // Update sensor values based on new rotation status.
  void RefreshSensors(double x, double y, double z);

  // Return a string with serialized sensors data in ascending order of
  // sensor id. A bitmask is used to specify which sensors to include.
  // Each bit maps to a sensor type, and a set bit indicates that the
  // corresponding sensor should be included in the returned data. Assuming
  // accelerometer and gyroscope are specified, the returned string would be
  // formatted as "<acc.x>:<acc.y>:<acc.z> <gyro.x>:<gyro.y>:<gyro.z>".
  std::string GetSensorsData(const SensorsMask mask);

 private:
  std::mutex sensors_data_mtx_;
  Eigen::Vector3d sensors_data_[kMaxSensorId + 1];
  Eigen::Matrix3d prior_rotation_matrix_, current_rotation_matrix_;
  std::chrono::time_point<std::chrono::high_resolution_clock>
      last_event_timestamp_;
};

}  // namespace sensors
}  // namespace cuttlefish