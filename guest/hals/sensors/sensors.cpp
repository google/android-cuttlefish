/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "guest/hals/sensors/vsoc_sensors.h"

#include <limits>

#include "guest/hals/sensors/sensors.h"

namespace cvd {
namespace {
const cvd::time::Milliseconds kDefaultSamplingRate(200);

timespec infinity() {
  timespec ts;
  ts.tv_sec = std::numeric_limits<time_t>::max();
  ts.tv_nsec = 0;
  return ts;
}
}  // namespace

const cvd::time::MonotonicTimePoint SensorState::kInfinity =
    cvd::time::MonotonicTimePoint(infinity());

SensorState::SensorState(SensorInfo info)
    : enabled_(false),
      event_(),
      deadline_(kInfinity),
      sampling_period_(kDefaultSamplingRate) {
  event_.sensor = info.handle;
  event_.type = info.type;
}

SensorInfo::SensorInfo(const char* name, const char* vendor, int version,
                       int handle, int type, float max_range, float resolution,
                       float power, int32_t min_delay,
                       uint32_t fifo_reserved_event_count,
                       uint32_t fifo_max_event_count, const char* string_type,
                       const char* required_permission, int32_t max_delay,
                       uint32_t reporting_mode) {
  this->name = name;
  this->vendor = vendor;
  this->version = version;
  this->handle = handle;
  this->type = type;
  this->maxRange = max_range;
  this->resolution = resolution;
  this->power = power;
  this->minDelay = min_delay;
  this->fifoReservedEventCount = fifo_reserved_event_count;
  this->fifoMaxEventCount = fifo_max_event_count;
  this->stringType = string_type;
  this->requiredPermission = required_permission;
  this->maxDelay = max_delay;
  this->flags = reporting_mode;
}

namespace sc = sensors_constants;

SensorInfo AccelerometerSensor() {
  uint32_t flags = sc::kAccelerometerReportingMode |
      (sc::kAccelerometerIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(sc::kAccelerometerName, sc::kVendor, sc::kVersion,
                    sc::kAccelerometerHandle, SENSOR_TYPE_ACCELEROMETER,
                    sc::kAccelerometerMaxRange, sc::kAccelerometerResolution,
                    sc::kAccelerometerPower, sc::kAccelerometerMinDelay,
                    sc::kFifoReservedEventCount, sc::kFifoMaxEventCount,
                    sc::kAccelerometerStringType, sc::kRequiredPermission,
                    sc::kMaxDelay, flags);
}

SensorInfo GyroscopeSensor() {
  uint32_t flags = sc::kGyroscopeReportingMode |
      (sc::kGyroscopeIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(
      sc::kGyroscopeName, sc::kVendor, sc::kVersion, sc::kGyroscopeHandle,
      SENSOR_TYPE_GYROSCOPE, sc::kGyroscopeMaxRange, sc::kGyroscopeResolution,
      sc::kGyroscopePower, sc::kGyroscopeMinDelay, sc::kFifoReservedEventCount,
      sc::kFifoMaxEventCount, sc::kGyroscopeStringType, sc::kRequiredPermission,
      sc::kMaxDelay, flags);
}

SensorInfo LightSensor() {
  uint32_t flags = sc::kLightReportingMode |
      (sc::kLightIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(sc::kLightName, sc::kVendor, sc::kVersion, sc::kLightHandle,
                    SENSOR_TYPE_LIGHT, sc::kLightMaxRange, sc::kLightResolution,
                    sc::kLightPower, sc::kLightMinDelay,
                    sc::kFifoReservedEventCount, sc::kFifoMaxEventCount,
                    sc::kLightStringType, sc::kRequiredPermission,
                    sc::kMaxDelay, flags);
}

SensorInfo MagneticFieldSensor() {
  uint32_t flags = sc::kMagneticFieldReportingMode |
      (sc::kMagneticFieldIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(sc::kMagneticFieldName, sc::kVendor, sc::kVersion,
                    sc::kMagneticFieldHandle, SENSOR_TYPE_MAGNETIC_FIELD,
                    sc::kMagneticFieldMaxRange, sc::kMagneticFieldResolution,
                    sc::kMagneticFieldPower, sc::kMagneticFieldMinDelay,
                    sc::kFifoReservedEventCount, sc::kFifoMaxEventCount,
                    sc::kMagneticFieldStringType, sc::kRequiredPermission,
                    sc::kMaxDelay, flags);
}

SensorInfo PressureSensor() {
  uint32_t flags = sc::kPressureReportingMode |
      (sc::kPressureIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(
      sc::kPressureName, sc::kVendor, sc::kVersion, sc::kPressureHandle,
      SENSOR_TYPE_PRESSURE, sc::kPressureMaxRange, sc::kPressureResolution,
      sc::kPressurePower, sc::kPressureMinDelay, sc::kFifoReservedEventCount,
      sc::kFifoMaxEventCount, sc::kPressureStringType, sc::kRequiredPermission,
      sc::kMaxDelay, flags);
}

SensorInfo ProximitySensor() {
  uint32_t flags = sc::kProximityReportingMode |
      (sc::kProximityIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(
      sc::kProximityName, sc::kVendor, sc::kVersion, sc::kProximityHandle,
      SENSOR_TYPE_PROXIMITY, sc::kProximityMaxRange, sc::kProximityResolution,
      sc::kProximityPower, sc::kProximityMinDelay, sc::kFifoReservedEventCount,
      sc::kFifoMaxEventCount, sc::kProximityStringType, sc::kRequiredPermission,
      sc::kMaxDelay, flags);
}

SensorInfo AmbientTempSensor() {
  uint32_t flags = sc::kAmbientTempReportingMode |
      (sc::kAmbientTempIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(sc::kAmbientTempName, sc::kVendor, sc::kVersion,
                    sc::kAmbientTempHandle, SENSOR_TYPE_AMBIENT_TEMPERATURE,
                    sc::kAmbientTempMaxRange, sc::kAmbientTempResolution,
                    sc::kAmbientTempPower, sc::kAmbientTempMinDelay,
                    sc::kFifoReservedEventCount, sc::kFifoMaxEventCount,
                    sc::kAmbientTempStringType, sc::kRequiredPermission,
                    sc::kMaxDelay, flags);
}

SensorInfo DeviceTempSensor() {
  uint32_t flags = sc::kDeviceTempReportingMode |
      (sc::kDeviceTempIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(sc::kDeviceTempName, sc::kVendor, sc::kVersion,
                    sc::kDeviceTempHandle, SENSOR_TYPE_TEMPERATURE,
                    sc::kDeviceTempMaxRange, sc::kDeviceTempResolution,
                    sc::kDeviceTempPower, sc::kDeviceTempMinDelay,
                    sc::kFifoReservedEventCount, sc::kFifoMaxEventCount,
                    sc::kDeviceTempStringType, sc::kRequiredPermission,
                    sc::kMaxDelay, flags);
}

SensorInfo RelativeHumiditySensor() {
  uint32_t flags = sc::kRelativeHumidityReportingMode |
      (sc::kRelativeHumidityIsWakeup ? SENSOR_FLAG_WAKE_UP : 0);

  return SensorInfo(sc::kRelativeHumidityName, sc::kVendor, sc::kVersion,
                    sc::kRelativeHumidityHandle, SENSOR_TYPE_RELATIVE_HUMIDITY,
                    sc::kRelativeHumidityMaxRange,
                    sc::kRelativeHumidityResolution, sc::kRelativeHumidityPower,
                    sc::kRelativeHumidityMinDelay, sc::kFifoReservedEventCount,
                    sc::kFifoMaxEventCount, sc::kRelativeHumidityStringType,
                    sc::kRequiredPermission, sc::kMaxDelay,
                    flags);
}

}  // namespace cvd
