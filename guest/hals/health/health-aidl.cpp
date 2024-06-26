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
#define LOG_TAG "android.hardware.health-service.cuttlefish"

#include <memory>
#include <string_view>

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

using ::aidl::android::hardware::health::BatteryHealth;
using ::aidl::android::hardware::health::BatteryStatus;
using ::aidl::android::hardware::health::HalHealthLoop;
using ::aidl::android::hardware::health::Health;
using ::aidl::android::hardware::health::HealthInfo;
using ::aidl::android::hardware::health::IHealth;
using ::android::hardware::health::InitHealthdConfig;
using ::ndk::ScopedAStatus;
using ::ndk::SharedRefBase;
using namespace std::literals;

namespace aidl::android::hardware::health {

// Health HAL implementation for cuttlefish. Note that in this implementation,
// cuttlefish pretends to be a device with a battery being charged.
// Implementations on real devices should not insert these fake values. For
// example, a battery-less device should report batteryPresent = false and
// batteryStatus = UNKNOWN.

class HealthImpl : public Health {
 public:
  // Inherit constructor.
  using Health::Health;
  virtual ~HealthImpl() {}

  ScopedAStatus getChargeCounterUah(int32_t* out) override;
  ScopedAStatus getCurrentNowMicroamps(int32_t* out) override;
  ScopedAStatus getCurrentAverageMicroamps(int32_t* out) override;
  ScopedAStatus getCapacity(int32_t* out) override;
  ScopedAStatus getChargeStatus(BatteryStatus* out) override;
  ScopedAStatus getBatteryHealthData(BatteryHealthData* out) override;

 protected:
  void UpdateHealthInfo(HealthInfo* health_info) override;
};

void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {
  health_info->chargerAcOnline = true;
  health_info->chargerUsbOnline = true;
  health_info->chargerWirelessOnline = false;
  health_info->maxChargingCurrentMicroamps = 500000;
  health_info->maxChargingVoltageMicrovolts = 5000000;
  health_info->batteryStatus = BatteryStatus::CHARGING;
  health_info->batteryHealth = BatteryHealth::GOOD;
  health_info->batteryPresent = true;
  health_info->batteryLevel = 85;
  health_info->batteryVoltageMillivolts = 3600;
  health_info->batteryTemperatureTenthsCelsius = 250;
  health_info->batteryCurrentMicroamps = 400000;
  health_info->batteryCycleCount = 32;
  health_info->batteryFullChargeUah = 4000000;
  health_info->batteryChargeCounterUah = 1900000;
  health_info->batteryTechnology = "Li-ion";
}

ScopedAStatus HealthImpl::getChargeCounterUah(int32_t* out) {
  *out = 1900000;
  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getCurrentNowMicroamps(int32_t* out) {
  *out = 400000;
  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getCurrentAverageMicroamps(int32_t*) {
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HealthImpl::getCapacity(int32_t* out) {
  *out = 85;
  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getChargeStatus(BatteryStatus* out) {
  *out = BatteryStatus::CHARGING;
  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getBatteryHealthData(BatteryHealthData* out) {
  out->batteryManufacturingDateSeconds =
      1689787603;  // Wednesday, 19 July 2023 17:26:43
  out->batteryFirstUsageSeconds =
      1691256403;  // Saturday, 5 August 2023 17:26:43
  out->batteryStateOfHealth = 99;
  out->batterySerialNumber =
      "d1f92fe7591ff096ca3a29c450a5a3d1";  // MD5("battery serial")
  out->batteryPartStatus = BatteryPartStatus::UNSUPPORTED;
  return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::health

int main(int, [[maybe_unused]] char** argv) {
#ifdef __ANDROID_RECOVERY__
  android::base::InitLogging(argv, android::base::KernelLogger);
#endif
  // Cuttlefish does not support offline-charging mode, hence do not handle
  // --charger option.
  using aidl::android::hardware::health::HealthImpl;
  LOG(INFO) << "Starting health HAL.";
  auto config = std::make_unique<healthd_config>();
  InitHealthdConfig(config.get());
  auto binder = SharedRefBase::make<HealthImpl>("default", std::move(config));
  auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
  return hal_health_loop->StartLoop();
}
