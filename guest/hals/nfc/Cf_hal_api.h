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

#ifndef _VENDOR_HAL_API_H_
#define _VENDOR_HAL_API_H_

#include <aidl/android/hardware/nfc/INfc.h>
#include <aidl/android/hardware/nfc/NfcConfig.h>
#include <aidl/android/hardware/nfc/NfcEvent.h>
#include <aidl/android/hardware/nfc/NfcStatus.h>
#include <aidl/android/hardware/nfc/PresenceCheckAlgorithm.h>
#include <aidl/android/hardware/nfc/ProtocolDiscoveryConfig.h>
#include "hardware_nfc.h"

using aidl::android::hardware::nfc::NfcConfig;
using aidl::android::hardware::nfc::NfcEvent;
using aidl::android::hardware::nfc::NfcStatus;
using aidl::android::hardware::nfc::PresenceCheckAlgorithm;
using aidl::android::hardware::nfc::ProtocolDiscoveryConfig;

int Cf_hal_open(nfc_stack_callback_t* p_cback,
                nfc_stack_data_callback_t* p_data_cback);
int Cf_hal_write(uint16_t data_len, const uint8_t* p_data);

int Cf_hal_core_initialized();

int Cf_hal_pre_discover();

int Cf_hal_close();

int Cf_hal_close_off();

int Cf_hal_power_cycle();

void Cf_hal_factoryReset();

void Cf_hal_getConfig(NfcConfig& config);

void Cf_hal_setVerboseLogging(bool enable);

bool Cf_hal_getVerboseLogging();

#endif /* _VENDOR_HAL_API_H_ */
