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
#pragma once

#include "common/libs/time/monotonic_time.h"
#include "guest/hals/sensors/sensors_hal.h"

namespace cvd {

// Stores static information about a sensor.
// Must be completely compatible with sensor_t (i.e. no additional
// information or virtual functions)
// so we can cast a list of SensorInfo to a list of sensor_t.
class SensorInfo : public sensor_t {
 public:
  // Dummy, empty set of sensor information (value-initialized).
  SensorInfo() : sensor_t() {}

 private:
  SensorInfo(const char* name, const char* vendor, int version, int handle,
             int type, float max_range, float resolution, float power,
             int32_t min_delay, uint32_t fifo_reserved_event_count,
             uint32_t fifo_max_event_count, const char* string_type,
             const char* required_permission, int32_t max_delay,
             uint32_t reporting_mode);

  friend SensorInfo AccelerometerSensor();
  friend SensorInfo GyroscopeSensor();
  friend SensorInfo LightSensor();
  friend SensorInfo MagneticFieldSensor();
  friend SensorInfo PressureSensor();
  friend SensorInfo ProximitySensor();
  friend SensorInfo AmbientTempSensor();
  friend SensorInfo DeviceTempSensor();
  friend SensorInfo RelativeHumiditySensor();
};

SensorInfo AccelerometerSensor();
SensorInfo GyroscopeSensor();
SensorInfo LightSensor();
SensorInfo MagneticFieldSensor();
SensorInfo PressureSensor();
SensorInfo ProximitySensor();
SensorInfo AmbientTempSensor();
SensorInfo DeviceTempSensor();
SensorInfo RelativeHumiditySensor();

// Stores the current state of a sensor.
class SensorState {
 public:
  SensorState(SensorInfo info);
  virtual ~SensorState() {}

  // What this sensor is activated or not.
  bool enabled_;
  // Buffer of incoming events.
  sensors_event_t event_;
  // The deadline at which we should report the next sensor event
  // to the framework in order to meet our frequency constraints.
  // For disabled sensors, should be 'infinity'.
  cvd::time::MonotonicTimePoint deadline_;
  // Delay time between consecutive sensor samples, in ns.
  cvd::time::Nanoseconds sampling_period_;

  // Time 'infinity'.
  static const cvd::time::MonotonicTimePoint kInfinity;
};

namespace sensors_constants {
// TODO: Verify these numbers.
// Vendor of the hardware part.
const char kVendor[] = "Google";
// Version of the hardware part + driver. The value of this field
// must increase when the driver is updated in a way that
// changes the output of the sensor.
const int kVersion = VSOC_SENSOR_DEVICE_VERSION;
// Number of events reserved for this sensor in batch mode FIFO.
// If it has its own FIFO, the size of that FIFO.
const uint32_t kFifoReservedEventCount = 15;
// Maximum events that can be batched. In a shared FIFO,
// the size of that FIFO.
const uint32_t kFifoMaxEventCount = 15;
// Permission required to use this sensor, or empty string
// if none required.
const char kRequiredPermission[] = "";
// Defined only for continuous mode and on-change sensors.
// Delay corresponding with lowest frequency supported.
const int32_t kMaxDelay = 5000000;

// Name of this sensor. Must be unique.
const char kAccelerometerName[] = "acceleration";
const char kGyroscopeName[] = "gyroscope";
const char kLightName[] = "light";
const char kMagneticFieldName[] = "magnetic_field";
const char kPressureName[] = "pressure";
const char kProximityName[] = "proximity";
const char kAmbientTempName[] = "ambient_temp";
const char kDeviceTempName[] = "device_temp";
const char kRelativeHumidityName[] = "relative_humidity";

// Handle that identifies the sensor. This is used as an array index,
// so must be unique in the range [0, # sensors)

const int kAccelerometerHandle = 0;
const int kGyroscopeHandle = 1;
const int kLightHandle = 2;
const int kMagneticFieldHandle = 3;
const int kPressureHandle = 4;
const int kProximityHandle = 5;
const int kAmbientTempHandle = 6;
const int kDeviceTempHandle = 7;
const int kRelativeHumidityHandle = 8;

// For continuous sensors, minimum sample period (in microseconds).
// On-Change (0), One-shot (-1), and special (0).
const int32_t kAccelerometerMinDelay = 4444;
const int32_t kGyroscopeMinDelay = 4444;
const int32_t kLightMinDelay = 0;
const int32_t kMagneticFieldMinDelay = 14285;
const int32_t kPressureMinDelay = 28571;
const int32_t kProximityMinDelay = 0;
const int32_t kAmbientTempMinDelay = 4444;
const int32_t kDeviceTempMinDelay = 4444;
const int32_t kRelativeHumidityMinDelay = 4444;

// Maximum range of this sensor's value in SI units.
const float kAccelerometerMaxRange = 39.226593f;
const float kGyroscopeMaxRange = 8.726639f;
const float kLightMaxRange = 10000.0f;
const float kMagneticFieldMaxRange = 4911.9995f;
const float kPressureMaxRange = 1100.0f;
const float kProximityMaxRange = 5.0f;
const float kAmbientTempMaxRange = 80.0f;
const float kDeviceTempMaxRange = 80.0f;
const float kRelativeHumidityMaxRange = 100;

// Smallest difference between two values reported by this sensor.
const float kAccelerometerResolution = 0.45f;
const float kGyroscopeResolution = 10.0f;
const float kLightResolution = 10.0f;
const float kMagneticFieldResolution = 1.0f;
const float kPressureResolution = 1.0f;
const float kProximityResolution = 1.0f;
const float kAmbientTempResolution = 1.0f;
const float kDeviceTempResolution = 1.0f;
const float kRelativeHumidityResolution = 1.0f;

// Rough estimate of this sensor's power consumption in mA.
const float kAccelerometerPower = 0.45f;
const float kGyroscopePower = 3.6f;
const float kLightPower = 0.175f;
const float kMagneticFieldPower = 5.0f;
const float kPressurePower = 0.004f;
const float kProximityPower = 12.675f;
const float kAmbientTempPower = 1.0f;
const float kDeviceTempPower = 1.0f;
const float kRelativeHumidityPower = 1.0f;

// Type of this sensor, represented as a string.

const char kAccelerometerStringType[] = SENSOR_STRING_TYPE_ACCELEROMETER;
const char kGyroscopeStringType[] = SENSOR_STRING_TYPE_GYROSCOPE;
const char kLightStringType[] = SENSOR_STRING_TYPE_LIGHT;
const char kMagneticFieldStringType[] = SENSOR_STRING_TYPE_MAGNETIC_FIELD;
const char kPressureStringType[] = SENSOR_STRING_TYPE_PRESSURE;
const char kProximityStringType[] = SENSOR_STRING_TYPE_PROXIMITY;
const char kAmbientTempStringType[] = SENSOR_STRING_TYPE_AMBIENT_TEMPERATURE;
const char kDeviceTempStringType[] = SENSOR_STRING_TYPE_TEMPERATURE;
const char kRelativeHumidityStringType[] = SENSOR_STRING_TYPE_RELATIVE_HUMIDITY;

const uint32_t kAccelerometerReportingMode = SENSOR_FLAG_CONTINUOUS_MODE;
const uint32_t kGyroscopeReportingMode = SENSOR_FLAG_CONTINUOUS_MODE;
const uint32_t kLightReportingMode = SENSOR_FLAG_ON_CHANGE_MODE;
const uint32_t kMagneticFieldReportingMode = SENSOR_FLAG_CONTINUOUS_MODE;
const uint32_t kPressureReportingMode = SENSOR_FLAG_CONTINUOUS_MODE;
const uint32_t kProximityReportingMode = SENSOR_FLAG_ON_CHANGE_MODE;
const uint32_t kAmbientTempReportingMode = SENSOR_FLAG_ON_CHANGE_MODE;
const uint32_t kDeviceTempReportingMode = SENSOR_FLAG_ON_CHANGE_MODE;
const uint32_t kRelativeHumidityReportingMode = SENSOR_FLAG_ON_CHANGE_MODE;

const bool kAccelerometerIsWakeup = false;
const bool kGyroscopeIsWakeup = false;
const bool kLightIsWakeup = false;
const bool kMagneticFieldIsWakeup = false;
const bool kPressureIsWakeup = false;
const bool kProximityIsWakeup = true;
const bool kAmbientTempIsWakeup = false;
const bool kDeviceTempIsWakeup = false;
const bool kRelativeHumidityIsWakeup = false;

}  // namespace sensors_constants
}  // namespace cvd

