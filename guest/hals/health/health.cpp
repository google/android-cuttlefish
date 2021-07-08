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

#include <fcntl.h>
#include <memory>
#include <string_view>

#include <android-base/logging.h>
#include <health/utils.h>
#include <health2impl/Health.h>
#include <utils/Log.h>

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

 private:
  std::string getValue(const char* path);
  bool getBool(const char* path);
  int getInt(const char* path);
  V1_0::BatteryStatus getBatteryStatus(const char* path);
  V1_0::BatteryHealth getBatteryHealth(const char* path);
};

std::string HealthImpl::getValue(const char* path) {
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    ALOGE("Failed to open %s (%s)", path, strerror(errno));
    return std::string();
  }

  char buf[256];
  ssize_t n = read(fd, buf, sizeof(buf));
  if (n < 0) {
    ALOGE("Failed to read %s (%s)", path, strerror(errno));
    return std::string();
  }
  close(fd);

  for (int i = 0; i < 256; i++) {
    if (buf[i] == '\n') {
      buf[i] = '\0';
    }
  }
  return std::string(buf);
}

bool HealthImpl::getBool(const char* path) {
  auto val = getValue(path);
  if (val.empty()) return false;
  return (val[0] == '1');
}

int HealthImpl::getInt(const char* path) {
  auto val = getValue(path);
  if (val.empty()) return 0;
  return std::stoi(val);
}

V1_0::BatteryStatus HealthImpl::getBatteryStatus(const char* path) {
  auto status = getValue(path);
  if (status == "Charging") return V1_0::BatteryStatus::CHARGING;
  if (status == "Discharging") return V1_0::BatteryStatus::DISCHARGING;
  if (status == "Not charging") return V1_0::BatteryStatus::NOT_CHARGING;
  if (status == "Full") return V1_0::BatteryStatus::FULL;
  return V1_0::BatteryStatus::UNKNOWN;
}

V1_0::BatteryHealth HealthImpl::getBatteryHealth(const char* path) {
  auto health = getValue(path);
  if (health == "Good") return V1_0::BatteryHealth::GOOD;
  if (health == "Overheat") return V1_0::BatteryHealth::OVERHEAT;
  if (health == "Dead") return V1_0::BatteryHealth::DEAD;
  if (health == "Over voltage") return V1_0::BatteryHealth::OVER_VOLTAGE;
  if (health == "Unspecified failure")
    return V1_0::BatteryHealth::UNSPECIFIED_FAILURE;
  if (health == "Cold") return V1_0::BatteryHealth::COLD;
  if (health == "Warm") return V1_0::BatteryHealth::GOOD;
  if (health == "Cool") return V1_0::BatteryHealth::GOOD;
  if (health == "Hot") return V1_0::BatteryHealth::OVERHEAT;
  return V1_0::BatteryHealth::UNKNOWN;
}

// Process updated health property values. This function is called when
// the kernel sends updated battery status via a uevent from the power_supply
// subsystem, or when updated values are polled by healthd, as for periodic
// poll of battery state.
//
// health_info contains properties read from the kernel. These values may
// be modified in this call, prior to sending the modified values to the
// Android runtime.

void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {
  auto* battery_props = &health_info->legacy.legacy;
  battery_props->chargerAcOnline = getBool("/sys/class/power_supply/ac/online");
  battery_props->chargerUsbOnline =
      getBool("/sys/class/power_supply/ac/online");
  battery_props->chargerWirelessOnline = false;
  battery_props->maxChargingCurrent = 500000;
  battery_props->maxChargingVoltage = 5000000;
  battery_props->batteryStatus =
      getBatteryStatus("/sys/class/power_supply/battery/status");
  battery_props->batteryHealth =
      getBatteryHealth("/sys/class/power_supply/battery/health");
  battery_props->batteryPresent =
      getBool("/sys/class/power_supply/battery/present");
  battery_props->batteryLevel =
      getInt("/sys/class/power_supply/battery/capacity");
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
  _hidl_cb(Result::SUCCESS, getInt("/sys/class/power_supply/battery/capacity"));
  return Void();
}

Return<void> HealthImpl::getChargeStatus(getChargeStatus_cb _hidl_cb) {
  _hidl_cb(Result::SUCCESS,
           getBatteryStatus("/sys/class/power_supply/battery/status"));
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
