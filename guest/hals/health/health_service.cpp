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
#define LOG_TAG "android.hardware.health@2.0-service.cuttlefish"

#include <health2/Health.h>
#include <health2/service.h>
#include <healthd/healthd.h>

#include <android-base/logging.h>

using android::hardware::health::V2_0::DiskStats;
using android::hardware::health::V2_0::StorageInfo;

void healthd_board_init(struct healthd_config*) {}

int healthd_board_battery_update(
    struct android::BatteryProperties* battery_props) {
  battery_props->chargerAcOnline = false;
  battery_props->chargerUsbOnline = true;
  battery_props->chargerWirelessOnline = false;
  battery_props->maxChargingCurrent = 500000;
  battery_props->maxChargingVoltage = 5000000;
  battery_props->batteryStatus = android::BATTERY_STATUS_CHARGING;
  battery_props->batteryHealth = android::BATTERY_HEALTH_GOOD;
  battery_props->batteryPresent = true;
  battery_props->batteryLevel = 85;
  battery_props->batteryVoltage = 3600;
  battery_props->batteryTemperature = 350;
  battery_props->batteryCurrent = 400000;
  battery_props->batteryCycleCount = 32;
  battery_props->batteryFullCharge = 4000000;
  battery_props->batteryChargeCounter = 1900000;
  battery_props->batteryTechnology = "Li-ion";
  return 0;
}

void get_storage_info(std::vector<struct StorageInfo>&) {}

void get_disk_stats(std::vector<struct DiskStats>&) {}

int main(void) { return health_service_main(); }
