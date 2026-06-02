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

#include "cuttlefish/host/commands/sensors_simulator/sensors_simulator.h"

#include <cmath>
#include <utility>

namespace cuttlefish {
namespace sensors {

namespace {
constexpr float kTemperature = 25.0f;   // celsius
constexpr float kProximity = 1.0f;      // cm
constexpr float kLight = 1000.0f;       // lux
constexpr float kPressure = 1013.25f;   // hpa
constexpr float kHumidity = 40.0f;      // percent
constexpr float kHingeAngle0 = 180.0f;  // degree
constexpr double kG = 9.80665;          // meter per second^2
const Eigen::Vector3d kMagneticField{0, 5.9, -48.4};
inline double ToRadians(double x) { return x * M_PI / 180; }

// Check if a given sensor id provides scalar data
static bool IsScalarSensor(int id) {
  return (id == kTemperatureId) || (id == kProximityId) || (id == kLightId) ||
         (id == kPressureId) || (id == kHumidityId) || (id == kHingeAngle0Id);
}

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
    Eigen::Matrix3d current_rotation_matrix, bool is_auto) {
  // For automotive devices, the Z-axis of the reference frame is aligned to
  // gravity. See
  // https://source.android.com/docs/core/interaction/sensors/sensor-types#auto_axes
  return current_rotation_matrix *
         (is_auto ? Eigen::Vector3d(0, 0, kG) : Eigen::Vector3d(0, kG, 0));
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

SensorsSimulator::SensorsSimulator(bool is_auto)
    : current_rotation_matrix_(GetRotationMatrix(0, 0, 0)),
      last_event_timestamp_(std::chrono::high_resolution_clock::now()),
      is_auto_(is_auto) {
  // Initialize sensors_data_ based on rotation vector = (0, 0, 0)
  SetMotion(0, 0, 0);
  // Set constant values for the sensors that are independent of rotation vector
  sensors_data_[kTemperatureId].f = kTemperature;
  sensors_data_[kProximityId].f = kProximity;
  sensors_data_[kLightId].f = kLight;
  sensors_data_[kPressureId].f = kPressure;
  sensors_data_[kHumidityId].f = kHumidity;
  sensors_data_[kHingeAngle0Id].f = kHingeAngle0;
}

void SensorsSimulator::SetSensorsChangedCallback(
    SensorsMask mask, SensorsChangedCallback callback) {
  sensors_changed_mask_ = mask;
  sensors_changed_callback_ = std::move(callback);
}

void SensorsSimulator::NotifySensorsChanged(SensorsMask changed) {
  SensorsMask relevant = changed & sensors_changed_mask_;
  if (sensors_changed_callback_ && relevant) {
    sensors_changed_callback_(relevant);
  }
}

void SensorsSimulator::SetMotion(double x, double y, double z) {
  Eigen::Matrix3d rotation_matrix_update = GetRotationMatrix(x, y, z);
  Eigen::Vector3d acc_update =
      CalculateAcceleration(rotation_matrix_update, is_auto_);
  Eigen::Vector3d mgn_update = CalculateMagnetometer(rotation_matrix_update);

  {
    std::lock_guard<std::mutex> lock(sensors_data_mtx_);
    auto current_time = std::chrono::high_resolution_clock::now();
    auto duration = current_time - last_event_timestamp_;
    last_event_timestamp_ = current_time;

    Eigen::Vector3d gyro_update = CalculateGyroscope(
        duration, current_rotation_matrix_, rotation_matrix_update);

    current_rotation_matrix_ = rotation_matrix_update;

    sensors_data_[kRotationVecId].v << x, y, z;
    sensors_data_[kAccelerationId].v = acc_update;
    sensors_data_[kGyroscopeId].v = gyro_update;
    sensors_data_[kMagneticId].v = mgn_update;

    // Copy the calibrated sensor data over for uncalibrated sensor support
    sensors_data_[kUncalibAccelerationId].v = acc_update;
    sensors_data_[kUncalibGyroscopeId].v = gyro_update;
    sensors_data_[kUncalibMagneticId].v = mgn_update;
  }
  NotifySensorsChanged((1 << kRotationVecId) | (1 << kAccelerationId) |
                       (1 << kGyroscopeId) | (1 << kMagneticId) |
                       (1 << kUncalibAccelerationId) |
                       (1 << kUncalibGyroscopeId) | (1 << kUncalibMagneticId));
}

void SensorsSimulator::SetHingeAngle(float angle) {
  {
    std::lock_guard<std::mutex> lock(sensors_data_mtx_);
    if (sensors_data_[kHingeAngle0Id].f == angle) {
      return;
    }
    sensors_data_[kHingeAngle0Id].f = angle;
  }
  NotifySensorsChanged(1 << kHingeAngle0Id);
}

std::string SensorsSimulator::GetSensorsData(const SensorsMask mask) {
  std::stringstream sensors_msg;
  std::lock_guard<std::mutex> lock(sensors_data_mtx_);
  for (int id = 0; id <= kMaxSensorId; id++) {
    if (mask & (1 << id)) {
      if (IsScalarSensor(id)) {
        float f = sensors_data_[id].f;
        sensors_msg << f << OUTER_DELIM;
      } else {
        Eigen::Vector3d v = sensors_data_[id].v;
        sensors_msg << v(0) << INNER_DELIM << v(1) << INNER_DELIM << v(2)
                    << OUTER_DELIM;
      }
    }
  }
  return sensors_msg.str();
}

}  // namespace sensors
}  // namespace cuttlefish
