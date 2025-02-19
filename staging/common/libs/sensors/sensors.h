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
#pragma once

#include "common/libs/fs/shared_fd.h"
#include "common/libs/transport/channel_sharedfd.h"

namespace cuttlefish {
namespace sensors {
/*
  These must align with Goldfish sensor IDs as we reuse Goldfish sensor HAL
  library for Cuttlefish sensor HAL. (See
  `device/generic/goldfish/hals/sensors/sensor_list.h`.)
*/
inline constexpr int kAccelerationId = 0;
inline constexpr int kGyroscopeId = 1;
inline constexpr int kMagneticId = 2;
inline constexpr int kUncalibMagneticId = 9;
inline constexpr int kUncalibGyroscopeId = 10;
inline constexpr int kUncalibAccelerationId = 17;
/*
  This is reserved specifically for Cuttlefish to identify the device
  orientation relative to the East-North-Up coordinates frame. This is
  not really a sensor but rather input from web UI for us to calculate
  IMU readings.
*/
inline constexpr int kRotationVecId = 31;
inline constexpr int kMaxSensorId = 31;

/*
  Each sensor ID also represent a bit offset for an app to specify sensors
  via a bitmask.
*/
using SensorsMask = int;

inline constexpr char INNER_DELIM = ':';
inline constexpr char OUTER_DELIM = ' ';

/* Sensors Commands */
inline constexpr int kUpdateRotationVec = 0;
inline constexpr int kGetSensorsData = 1;

using SensorsCmd = int;

}  // namespace sensors
}  // namespace cuttlefish
