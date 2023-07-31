/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <cmath>
#include <thread>

#include <android-base/chrono_utils.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android-base/parsedouble.h>
#include <android-base/parseint.h>
#include <utils/SystemClock.h>
#include <aidl/android/hardware/sensors/BnSensors.h>

using aidl::android::hardware::sensors::Event;
using aidl::android::hardware::sensors::ISensors;
using aidl::android::hardware::sensors::SensorInfo;
using aidl::android::hardware::sensors::SensorStatus;
using aidl::android::hardware::sensors::SensorType;

std::shared_ptr<ISensors> startSensorInjection() {
  auto sensors = ISensors::fromBinder(ndk::SpAIBinder(
      AServiceManager_getService("android.hardware.sensors.ISensors/default")));
  CHECK(sensors != nullptr) << "Unable to get ISensors.";

  // Place the ISensors HAL into DATA_INJECTION mode so that we can
  // inject events.
  auto result =
      sensors->setOperationMode(ISensors::OperationMode::DATA_INJECTION);
  CHECK(result.isOk())
      << "Unable to set ISensors operation mode to DATA_INJECTION: "
      << result.getDescription();

  return sensors;
}

int getSensorHandle(SensorType type, const std::shared_ptr<ISensors> sensors) {
  // Find the first available sensor of the given type.
  int handle = -1;
  std::vector<SensorInfo> sensors_list;
  auto result = sensors->getSensorsList(&sensors_list);
  CHECK(result.isOk()) << "Unable to get ISensors sensors list: "
                        << result.getDescription();
  for (const SensorInfo& sensor : sensors_list) {
    if (sensor.type == type) {
      handle = sensor.sensorHandle;
      break;
    }
  }
  CHECK(handle != -1) << "Unable to find sensor.";
  return handle;
}

void endSensorInjection(const std::shared_ptr<ISensors> sensors) {
  // Return the ISensors HAL back to NORMAL mode.
  auto result = sensors->setOperationMode(ISensors::OperationMode::NORMAL);
  CHECK(result.isOk()) << "Unable to set sensors operation mode to NORMAL: "
                       << result.getDescription();
}

// Inject ACCELEROMETER events to corresponding to a given physical
// device position.
void InjectOrientation(int rotationDeg) {
  auto rad = M_PI * rotationDeg / 180.0;
  auto sensors = startSensorInjection();
  int handle = getSensorHandle(SensorType::ACCELEROMETER, sensors);

  // Create a base ISensors accelerometer event.
  Event event;
  event.sensorHandle = handle;
  event.sensorType = SensorType::ACCELEROMETER;
  Event::EventPayload::Vec3 vec3;
  // (x^2 + y^2 + z^2)^1/2 = ~9.8 = 1G
  vec3.x = 9.2 * std::sin(rad);
  vec3.y = 9.2 * std::cos(rad);
  // z is fixed at 3.5, meaning the device is positioned vertically with a
  // slight inclination backwards.
  vec3.z = 3.5;
  vec3.status = SensorStatus::ACCURACY_HIGH;
  event.payload.set<Event::EventPayload::Tag::vec3>(vec3);

  // Repeatedly inject accelerometer events. The WindowManager orientation
  // listener responds to sustained accelerometer data, not just a single event.
  android::base::Timer timer;
  while (timer.duration() < 1s) {
    event.timestamp = android::elapsedRealtimeNano();
    auto result = sensors->injectSensorData(event);
    CHECK(result.isOk()) << "Unable to inject ISensors accelerometer event: "
                         << result.getDescription();
    std::this_thread::sleep_for(10ms);
  }

  endSensorInjection(sensors);
}

// Inject accelerometer event based on rotation in device position.
void InjectAccelerometer(double x, double y, double z) {
  auto sensors = startSensorInjection();
  int handle = getSensorHandle(SensorType::ACCELEROMETER, sensors);
  Event event;
  event.sensorHandle = handle;
  event.sensorType = SensorType::ACCELEROMETER;

  Event::EventPayload::Vec3 vec3;
  vec3.x = x;
  vec3.y = y;
  vec3.z = z;
  vec3.status = SensorStatus::ACCURACY_HIGH;
  event.payload.set<Event::EventPayload::Tag::vec3>(vec3);
  event.timestamp = android::elapsedRealtimeNano();
  auto result = sensors->injectSensorData(event);
  CHECK(result.isOk()) << "Unable to inject ISensors accelerometer event: "
                       << result.getDescription();
}

// Inject Magnetometer event based on rotation in device position.
void InjectMagnetometer(double x, double y, double z) {
  auto sensors = startSensorInjection();
  int handle = getSensorHandle(SensorType::MAGNETIC_FIELD, sensors);
  Event event;
  event.sensorHandle = handle;
  event.sensorType = SensorType::MAGNETIC_FIELD;

  Event::EventPayload::Vec3 vec3;
  vec3.x = x;
  vec3.y = y;
  vec3.z = z;
  vec3.status = SensorStatus::ACCURACY_HIGH;
  event.payload.set<Event::EventPayload::Tag::vec3>(vec3);
  event.timestamp = android::elapsedRealtimeNano();
  auto result = sensors->injectSensorData(event);
  CHECK(result.isOk()) << "Unable to inject ISensors magnetometer event: "
                       << result.getDescription();
}

// Inject Gyroscope event based on rotation in device position.
void InjectGyroscope(double x, double y, double z){
  auto sensors = startSensorInjection();
  int handle = getSensorHandle(SensorType::GYROSCOPE, sensors);
  Event event;
  event.sensorHandle = handle;
  event.sensorType = SensorType::GYROSCOPE;

  Event::EventPayload::Vec3 vec3;
  vec3.x = x;
  vec3.y = y;
  vec3.z = z;
  vec3.status = SensorStatus::ACCURACY_HIGH;
  event.payload.set<Event::EventPayload::Tag::vec3>(vec3);
  event.timestamp = android::elapsedRealtimeNano();
  auto result = sensors->injectSensorData(event);
  CHECK(result.isOk()) << "Unable to inject ISensors gyroscope event: "
                       << result.getDescription();
}

// Inject a single HINGE_ANGLE event at the given angle.
void InjectHingeAngle(int angle) {
  auto sensors = startSensorInjection();
  int handle = getSensorHandle(SensorType::HINGE_ANGLE, sensors);

  // Create a base ISensors hinge_angle event.
  Event event;
  event.sensorHandle = handle;
  event.sensorType = SensorType::HINGE_ANGLE;
  event.payload.set<Event::EventPayload::Tag::scalar>((float)angle);
  event.timestamp = android::elapsedRealtimeNano();

  auto result = sensors->injectSensorData(event);
  CHECK(result.isOk()) << "Unable to inject HINGE_ANGLE data"
                       << result.getDescription();

  endSensorInjection(sensors);
}

int main(int argc, char** argv) {
  ::android::base::InitLogging(
      argv, android::base::LogdLogger(android::base::SYSTEM));
  CHECK(argc == 3 || argc == 11)
      << "Expected command line args 'rotate <angle>', 'hinge_angle <value>', or 'motion " <<
          "<acc_x> <acc_y> <acc_z> <mgn_x> <mgn_y> <mgn_z> <gyro_x> <gyro_y> <gyro_z>'";
  if (!strcmp(argv[1], "rotate")) {
    int rotationDeg;
    CHECK(android::base::ParseInt(argv[2], &rotationDeg))
        << "Rotation angle must be an integer";
    InjectOrientation(rotationDeg);
  } else if (!strcmp(argv[1], "hinge_angle")) {
    int angle;
    CHECK(android::base::ParseInt(argv[2], &angle))
        << "Hinge angle must be an integer";
    CHECK(angle >= 0 && angle <= 360) << "Bad hinge_angle value: " << argv[2];
    InjectHingeAngle(angle);
  } else if (!strcmp(argv[1], "motion")) {
    double acc_x, acc_y, acc_z, mgn_x, mgn_y, mgn_z, gyro_x, gyro_y, gyro_z;
    CHECK(android::base::ParseDouble(argv[2], &acc_x)) << "Accelerometer x value must be a double";
    CHECK(android::base::ParseDouble(argv[3], &acc_y)) << "Accelerometer x value must be a double";
    CHECK(android::base::ParseDouble(argv[4], &acc_z)) << "Accelerometer x value must be a double";
    CHECK(android::base::ParseDouble(argv[5], &mgn_x)) << "Magnetometer x value must be a double";
    CHECK(android::base::ParseDouble(argv[6], &mgn_y)) << "Magnetometer y value must be a double";
    CHECK(android::base::ParseDouble(argv[7], &mgn_z)) << "Magnetometer z value must be a double";
    CHECK(android::base::ParseDouble(argv[8], &gyro_x)) << "Gyroscope x value must be a double";
    CHECK(android::base::ParseDouble(argv[9], &gyro_y)) << "Gyroscope y value must be a double";
    CHECK(android::base::ParseDouble(argv[10], &gyro_z)) << "Gyroscope z value must be a double";
    InjectAccelerometer(acc_x, acc_y, acc_z);
    InjectMagnetometer(mgn_x, mgn_y, mgn_z);
    InjectGyroscope(gyro_x, gyro_y, gyro_z);
  } else {
    LOG(FATAL) << "Unknown arg: " << argv[1];
  }
}
