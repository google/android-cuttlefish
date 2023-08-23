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

#include "host/frontend/webrtc/sensors_simulator.h"

#include <android-base/logging.h>

#include <cmath>

namespace cuttlefish {
namespace webrtc_streaming {
namespace {

constexpr double kG = 9.80665;  // meter per second^2
const Eigen::Vector3d kGravityVec{0, kG, 0}, kMagneticField{0, 5.9, -48.4};

inline double toRadians(double x) { return x * M_PI / 180; }

// Calculate the rotation matrix of the pitch, roll, and yaw angles.
static Eigen::Matrix3d getRotationMatrix(double x, double y, double z) {
  x = toRadians(-x);
  y = toRadians(-y);
  z = toRadians(-z);
  // Create rotation matrices for each Euler angle
  Eigen::Matrix3d rx = Eigen::AngleAxisd(x, Eigen::Vector3d::UnitX()).toRotationMatrix();
  Eigen::Matrix3d ry = Eigen::AngleAxisd(y, Eigen::Vector3d::UnitY()).toRotationMatrix();
  Eigen::Matrix3d rz = Eigen::AngleAxisd(z, Eigen::Vector3d::UnitZ()).toRotationMatrix();

  return rz * (ry * rx);
}

// Calculate new Accelerometer values of the new rotation degrees.
static inline Eigen::Vector3d calculateAcceleration(Eigen::Matrix3d current_rotation_matrix) {
  return current_rotation_matrix * kGravityVec;
}

// Calculate new Magnetometer values of the new rotation degrees.
static inline Eigen::Vector3d calculateMagnetometer(Eigen::Matrix3d current_rotation_matrix) {
  return current_rotation_matrix * kMagneticField;
}

// Calculate new Gyroscope values of the new rotation degrees.
static Eigen::Vector3d calculateGyroscope(std::chrono::duration<double> duration,
                                          Eigen::Matrix3d prior_rotation_matrix,
                                          Eigen::Matrix3d current_rotation_matrix) {
  double time_diff = duration.count();
  if (time_diff == 0) {
    return Eigen::Vector3d{0, 0, 0};
  }
  Eigen::Matrix3d transition_matrix = prior_rotation_matrix * current_rotation_matrix.inverse();
  // Convert rotation matrix to angular velocity numerator.
  Eigen::AngleAxisd angle_axis(transition_matrix);
  double angle = angle_axis.angle();
  Eigen::Vector3d gyro = angle_axis.axis();
  gyro *= angle;
  gyro /= time_diff;
  return gyro;
}

std::string SerializeVector(const Eigen::Vector3d& v) {
  std::stringstream s;
  s << v(0) << " " << v(1) << " " << v(2);
  return s.str();
}

}  // namespace

SensorsSimulator::SensorsSimulator()
    : current_rotation_matrix_(getRotationMatrix(0, 0, 0)),
      last_event_timestamp_(std::chrono::high_resolution_clock::now()) {}

// Update sensor values based on new rotation status.
void SensorsSimulator::RefreshSensors(double x, double y, double z) {
  xyz_ << x, y, z;
  prior_rotation_matrix_ = current_rotation_matrix_;
  current_rotation_matrix_ = getRotationMatrix(x, y, z);
  acc_xyz_ = calculateAcceleration(current_rotation_matrix_);
  mgn_xyz_ = calculateMagnetometer(current_rotation_matrix_);
  auto current_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = current_time - last_event_timestamp_;
  gyro_xyz_ = calculateGyroscope(duration, prior_rotation_matrix_,
                                 current_rotation_matrix_);
  last_event_timestamp_ = current_time;
}

// Get sensors' data in string format to be passed as a message.
std::string SensorsSimulator::GetSensorsData() {
  std::stringstream sensors_data;
  sensors_data << SerializeVector(xyz_) << " " << SerializeVector(acc_xyz_) << " "
               << SerializeVector(mgn_xyz_) << " " << SerializeVector(gyro_xyz_);
  return sensors_data.str();
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
