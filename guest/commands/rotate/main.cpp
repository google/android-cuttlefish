/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "android/hardware/sensors/2.0/ISensors.h"

using android::sp;
using android::hardware::sensors::V1_0::Event;
using android::hardware::sensors::V1_0::OperationMode;
using android::hardware::sensors::V1_0::Result;
using android::hardware::sensors::V1_0::SensorInfo;
using android::hardware::sensors::V1_0::SensorStatus;
using android::hardware::sensors::V1_0::SensorType;
using android::hardware::sensors::V2_0::ISensors;

void InjectOrientation(bool portrait) {
  const sp<ISensors> sensors = ISensors::getService();
  if (sensors == nullptr) {
    LOG(FATAL) << "Unable to get ISensors.";
  }

  Result result;

  // Place the ISensors HAL into DATA_INJECTION mode so that we can
  // inject events.
  result = sensors->setOperationMode(OperationMode::DATA_INJECTION);
  if (result != Result::OK) {
    LOG(FATAL) << "Unable to set ISensors operation mode to DATA_INJECTION: "
               << toString(result);
  }

  // Find the first available accelerometer sensor.
  int accel_handle = -1;
  const auto& getSensorsList_result =
      sensors->getSensorsList([&](const auto& list) {
        for (const SensorInfo& sensor : list) {
          if (sensor.type == SensorType::ACCELEROMETER) {
            accel_handle = sensor.sensorHandle;
            break;
          }
        }
      });
  if (!getSensorsList_result.isOk()) {
    LOG(FATAL) << "Unable to get ISensors sensors list: "
               << getSensorsList_result.description();
  }
  if (accel_handle == -1) {
    LOG(FATAL) << "Unable to find ACCELEROMETER sensor.";
  }

  // Create a base ISensors accelerometer event.
  Event event;
  event.sensorHandle = accel_handle;
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
  while (timer.duration() < 1s) {
    event.timestamp = android::elapsedRealtimeNano();
    result = sensors->injectSensorData(event);
    if (result != Result::OK) {
      LOG(FATAL) << "Unable to inject ISensors accelerometer event: "
                 << toString(result);
    }
    std::this_thread::sleep_for(10ms);
  }

  // Return the ISensors HAL back to NORMAL mode.
  result = sensors->setOperationMode(OperationMode::NORMAL);
  if (result != Result::OK) {
    LOG(FATAL) << "Unable to set sensors operation mode to NORMAL: "
               << toString(result);
  }
}

int main(int argc, char** argv) {
  if (argc == 1) {
    LOG(FATAL) << "Expected command line arg 'portrait' or 'landscape'";
  }

  bool portrait = true;
  if (!strcmp(argv[1], "portrait")) {
    portrait = true;
  } else if (!strcmp(argv[1], "landscape")) {
    portrait = false;
  } else {
    LOG(FATAL) << "Expected command line arg 'portrait' or 'landscape'";
  }

  InjectOrientation(portrait);
}
