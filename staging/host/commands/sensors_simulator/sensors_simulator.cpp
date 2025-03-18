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

#include "host/commands/sensors_simulator/sensors_simulator.h"

#include <cmath>

#include <android-base/logging.h>

namespace cuttlefish {
namespace sensors {

namespace {

constexpr double kG = 9.80665;  // meter per second^2
const Eigen::Vector3d kGravityVec{0, kG, 0}, kMagneticField{0, 5.9, -48.4};

inline double ToRadians(double x) { return x * M_PI / 180; }

// Calculate the rotation matrix of the pitch, roll, and yaw angles.
static Eigen::Matrix3d GetRotationMatrix(double x, double y, double z) {
  x = ToRadians(-x);
  y = ToRadians(-y);
  z = ToRadians(-z);
  // Create rotation matrices for each Euler angle
  Eigen::Matrix3d rx =
      Eigen::AngleAxisd(x, Eigen::Vector3d::UnitX()).toRotationMatrix();
  Eigen::Matrix3d ry =
      Eigen::AngleAxisd(y, Eigen::Vector3d::UnitY()).toRotationMatrix();
  Eigen::Matrix3d rz =
      Eigen::AngleAxisd(z, Eigen::Vector3d::UnitZ()).toRotationMatrix();

  return rz * (ry * rx);
}

// Calculate new Accelerometer values of the new rotation degrees.
static inline Eigen::Vector3d CalculateAcceleration(
    Eigen::Matrix3d current_rotation_matrix) {
  return current_rotation_matrix * kGravityVec;
}

// Calculate new Magnetometer values of the new rotation degrees.
static inline Eigen::Vector3d CalculateMagnetometer(
    Eigen::Matrix3d current_rotation_matrix) {
  return current_rotation_matrix * kMagneticField;
}

// Calculate new Gyroscope values of the new rotation degrees.
static Eigen::Vector3d CalculateGyroscope(
    std::chrono::duration<double> duration,
    Eigen::Matrix3d prior_rotation_matrix,
    Eigen::Matrix3d current_rotation_matrix) {
  double time_diff = duration.count();
  if (time_diff == 0) {
    return Eigen::Vector3d{0, 0, 0};
  }
  Eigen::Matrix3d transition_matrix =
      prior_rotation_matrix * current_rotation_matrix.inverse();
  // Convert rotation matrix to angular velocity numerator.
  Eigen::AngleAxisd angle_axis(transition_matrix);
  double angle = angle_axis.angle();
  Eigen::Vector3d gyro = angle_axis.axis();
  gyro *= angle;
  gyro /= time_diff;
  return gyro;
}
}  // namespace

SensorsSimulator::SensorsSimulator()
    : current_rotation_matrix_(GetRotationMatrix(0, 0, 0)),
      last_event_timestamp_(std::chrono::high_resolution_clock::now()) {
  // Initialize sensors_data_ based on rotation vector = (0, 0, 0)
  RefreshSensors(0, 0, 0);
}

void SensorsSimulator::RefreshSensors(double x, double y, double z) {
  auto rotation_matrix_update = GetRotationMatrix(x, y, z);
  auto acc_update = CalculateAcceleration(rotation_matrix_update);
  auto mgn_update = CalculateMagnetometer(rotation_matrix_update);

  std::lock_guard<std::mutex> lock(sensors_data_mtx_);
  auto current_time = std::chrono::high_resolution_clock::now();
  auto duration = current_time - last_event_timestamp_;
  last_event_timestamp_ = current_time;

  auto gyro_update = CalculateGyroscope(duration, current_rotation_matrix_,
                                        rotation_matrix_update);

  current_rotation_matrix_ = rotation_matrix_update;

  sensors_data_[kRotationVecId] << x, y, z;
  sensors_data_[kAccelerationId] = acc_update;
  sensors_data_[kGyroscopeId] = gyro_update;
  sensors_data_[kMagneticId] = mgn_update;

  // Copy the calibrated sensor data over for uncalibrated sensor support
  sensors_data_[kUncalibAccelerationId] = acc_update;
  sensors_data_[kUncalibGyroscopeId] = gyro_update;
  sensors_data_[kUncalibMagneticId] = mgn_update;
}

std::string SensorsSimulator::GetSensorsData(const SensorsMask mask) {
  std::stringstream sensors_msg;
  std::lock_guard<std::mutex> lock(sensors_data_mtx_);
  for (int id = 0; id <= kMaxSensorId; id++) {
    if (mask & (1 << id)) {
      auto v = sensors_data_[id];
      sensors_msg << v(0) << INNER_DELIM << v(1) << INNER_DELIM << v(2)
                  << OUTER_DELIM;
    }
  }
  return sensors_msg.str();
}

}  // namespace sensors
}  // namespace cuttlefish