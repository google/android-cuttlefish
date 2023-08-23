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

#include <Eigen/Dense>

#include <chrono>
#include <string>

namespace cuttlefish {
namespace webrtc_streaming {

class SensorsSimulator {
 public:
  SensorsSimulator();
  // Update sensor values based on new rotation status.
  void RefreshSensors(double x, double y, double z);
  // Get sensors data in string format to be passed as a message.
  std::string GetSensorsData();

 private:
  Eigen::Vector3d xyz_ {0, 0, 0}, acc_xyz_{0, 0, 0}, mgn_xyz_{0, 0, 0}, gyro_xyz_{0, 0, 0};
  Eigen::Matrix3d prior_rotation_matrix_, current_rotation_matrix_;
  std::chrono::time_point<std::chrono::high_resolution_clock> last_event_timestamp_;
};
}  // namespace webrtc_streaming
}  // namespace cuttlefish
