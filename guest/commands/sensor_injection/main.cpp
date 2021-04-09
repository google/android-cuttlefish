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

#include <android-base/chrono_utils.h>
#include <android-base/logging.h>
#include <binder/IServiceManager.h>
#include <utils/StrongPointer.h>
#include <utils/SystemClock.h>

#include <thread>

#include "android/hardware/sensors/2.1/ISensors.h"

using android::sp;
using android::hardware::sensors::V1_0::OperationMode;
using android::hardware::sensors::V1_0::Result;
using android::hardware::sensors::V1_0::SensorStatus;
using android::hardware::sensors::V2_1::Event;
using android::hardware::sensors::V2_1::ISensors;
using android::hardware::sensors::V2_1::SensorInfo;
using android::hardware::sensors::V2_1::SensorType;

sp<ISensors> startSensorInjection() {
  const sp<ISensors> sensors = ISensors::getService();
  if (sensors == nullptr) {
    LOG(FATAL) << "Unable to get ISensors.";
  }

  // Place the ISensors HAL into DATA_INJECTION mode so that we can
  // inject events.
  Result result = sensors->setOperationMode(OperationMode::DATA_INJECTION);
  if (result != Result::OK) {
    LOG(FATAL) << "Unable to set ISensors operation mode to DATA_INJECTION: "
               << toString(result);
  }

  return sensors;
}

int getSensorHandle(SensorType type, const sp<ISensors> sensors) {
  // Find the first available sensor of the given type.
  int handle = -1;
  const auto& getSensorsList_result =
      sensors->getSensorsList_2_1([&](const auto& list) {
        for (const SensorInfo& sensor : list) {
          if (sensor.type == type) {
            handle = sensor.sensorHandle;
            break;
          }
        }
      });
  if (!getSensorsList_result.isOk()) {
    LOG(FATAL) << "Unable to get ISensors sensors list: "
               << getSensorsList_result.description();
  }
  if (handle == -1) {
    LOG(FATAL) << "Unable to find sensor.";
  }
  return handle;
}

void endSensorInjection(const sp<ISensors> sensors) {
  // Return the ISensors HAL back to NORMAL mode.
  Result result = sensors->setOperationMode(OperationMode::NORMAL);
  if (result != Result::OK) {
    LOG(FATAL) << "Unable to set sensors operation mode to NORMAL: "
               << toString(result);
  }
}

// Inject ACCELEROMETER events to corresponding to a given physical
// device orientation: portrait or landscape.
void InjectOrientation(bool portrait) {
  sp<ISensors> sensors = startSensorInjection();
  int handle = getSensorHandle(SensorType::ACCELEROMETER, sensors);

  // Create a base ISensors accelerometer event.
  Event event;
  event.sensorHandle = handle;
  event.sensorType = SensorType::ACCELEROMETER;
  if (portrait) {
    event.u.vec3.x = 0;
    event.u.vec3.y = 9.2;
  } else {
    event.u.vec3.x = 9.2;
    event.u.vec3.y = 0;
  }
  event.u.vec3.z = 3.5;
  event.u.vec3.status = SensorStatus::ACCURACY_HIGH;

  // Repeatedly inject accelerometer events. The WindowManager orientation
  // listener responds to sustained accelerometer data, not just a single event.
  android::base::Timer timer;
  Result result;
  while (timer.duration() < 1s) {
    event.timestamp = android::elapsedRealtimeNano();
    result = sensors->injectSensorData_2_1(event);
    if (result != Result::OK) {
      LOG(FATAL) << "Unable to inject ISensors accelerometer event: "
                 << toString(result);
    }
    std::this_thread::sleep_for(10ms);
  }

  endSensorInjection(sensors);
}

// Inject a single HINGE_ANGLE event at the given angle.
void InjectHingeAngle(int angle) {
  sp<ISensors> sensors = startSensorInjection();
  int handle = getSensorHandle(SensorType::HINGE_ANGLE, sensors);

  // Create a base ISensors hinge_angle event.
  Event event;
  event.sensorHandle = handle;
  event.sensorType = SensorType::HINGE_ANGLE;
  event.u.scalar = angle;
  event.u.vec3.status = SensorStatus::ACCURACY_HIGH;
  event.timestamp = android::elapsedRealtimeNano();
  Result result = sensors->injectSensorData_2_1(event);
  if (result != Result::OK) {
    LOG(FATAL) << "Unable to inject HINGE_ANGLE data: " << toString(result);
  }

  endSensorInjection(sensors);
}

int main(int argc, char** argv) {
  if (argc == 2) {
    LOG(FATAL) << "Expected command line args 'rotate <portrait|landscape>' or "
                  "'hinge_angle <value>'";
  }

  if (!strcmp(argv[1], "rotate")) {
    bool portrait = true;
    if (!strcmp(argv[2], "portrait")) {
      portrait = true;
    } else if (!strcmp(argv[2], "landscape")) {
      portrait = false;
    } else {
      LOG(FATAL) << "Expected command line arg 'portrait' or 'landscape'";
    }
    InjectOrientation(portrait);
  } else if (!strcmp(argv[1], "hinge_angle")) {
    int angle = std::stoi(argv[2]);
    if (angle < 0 || angle > 360) {
      LOG(FATAL) << "Bad hinge_angle value: " << argv[2];
    }
    InjectHingeAngle(angle);
  } else {
    LOG(FATAL) << "Unknown arg: " << argv[1];
  }
}
