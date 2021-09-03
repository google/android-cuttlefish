/*
 * Copyright (C) 2018 The Android Open Source Project
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
#define LOG_TAG "android.hardware.health@2.1-service.cuttlefish"

#include <memory>
#include <string_view>

#include <android-base/logging.h>
#include <health/utils.h>
#include <health2impl/Health.h>

using ::android::sp;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::health::InitHealthdConfig;
using ::android::hardware::health::V1_0::BatteryStatus;
using ::android::hardware::health::V2_0::Result;
using ::android::hardware::health::V2_1::IHealth;
using namespace std::literals;

namespace android {
namespace hardware {
namespace health {
namespace V2_1 {
namespace implementation {

// Health HAL implementation for cuttlefish. Note that in this implementation, cuttlefish
// pretends to be a device with a battery being charged. Implementations on real devices
// should not insert these fake values. For example, a battery-less device should report
// batteryPresent = false and batteryStatus = UNKNOWN.

class HealthImpl : public Health {
 public:
  HealthImpl(std::unique_ptr<healthd_config>&& config)
    : Health(std::move(config)) {}
    Return<void> getChargeCounter(getChargeCounter_cb _hidl_cb) override;
    Return<void> getCurrentNow(getCurrentNow_cb _hidl_cb) override;
    Return<void> getCapacity(getCapacity_cb _hidl_cb) override;
    Return<void> getChargeStatus(getChargeStatus_cb _hidl_cb) override;
 protected:
  void UpdateHealthInfo(HealthInfo* health_info) override;
};

void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {
  auto* battery_props = &health_info->legacy.legacy;
  battery_props->chargerAcOnline = true;
  battery_props->chargerUsbOnline = true;
  battery_props->chargerWirelessOnline = false;
  battery_props->maxChargingCurrent = 500000;
  battery_props->maxChargingVoltage = 5000000;
  battery_props->batteryStatus = V1_0::BatteryStatus::CHARGING;
  battery_props->batteryHealth = V1_0::BatteryHealth::GOOD;
  battery_props->batteryPresent = true;
  battery_props->batteryLevel = 85;
  battery_props->batteryVoltage = 3600;
  battery_props->batteryTemperature = 350;
  battery_props->batteryCurrent = 400000;
  battery_props->batteryCycleCount = 32;
  battery_props->batteryFullCharge = 4000000;
  battery_props->batteryChargeCounter = 1900000;
  battery_props->batteryTechnology = "Li-ion";
}

Return<void> HealthImpl::getChargeCounter(getChargeCounter_cb _hidl_cb) {
  _hidl_cb(Result::SUCCESS, 1900000);
  return Void();
}

Return<void> HealthImpl::getCurrentNow(getCurrentNow_cb _hidl_cb) {
  _hidl_cb(Result::SUCCESS, 400000);
  return Void();
}

Return<void> HealthImpl::getCapacity(getCapacity_cb _hidl_cb) {
  _hidl_cb(Result::SUCCESS, 85);
  return Void();
}

Return<void> HealthImpl::getChargeStatus(getChargeStatus_cb _hidl_cb) {
  _hidl_cb(Result::SUCCESS, BatteryStatus::CHARGING);
  return Void();
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace health
}  // namespace hardware
}  // namespace android


extern "C" IHealth* HIDL_FETCH_IHealth(const char* instance) {
  using ::android::hardware::health::V2_1::implementation::HealthImpl;
  if (instance != "default"sv) {
      return nullptr;
  }
  auto config = std::make_unique<healthd_config>();
  InitHealthdConfig(config.get());

  return new HealthImpl(std::move(config));
}
