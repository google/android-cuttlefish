/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <libradiocompat/RadioNetwork.h>

namespace cf::ril {

class RefRadioNetwork : public android::hardware::radio::compat::RadioNetwork {
    ::aidl::android::hardware::radio::network::UsageSetting mUsageSetting =
            ::aidl::android::hardware::radio::network::UsageSetting::VOICE_CENTRIC;

  public:
    using android::hardware::radio::compat::RadioNetwork::RadioNetwork;

    ::ndk::ScopedAStatus setUsageSetting(
            int32_t serial,
            ::aidl::android::hardware::radio::network::UsageSetting usageSetting) override;
    ::ndk::ScopedAStatus getUsageSetting(int32_t serial) override;

    ::ndk::ScopedAStatus setEmergencyMode(
            int32_t serial,
            ::aidl::android::hardware::radio::network::EmergencyMode emergencyMode) override;

    ::ndk::ScopedAStatus triggerEmergencyNetworkScan(
            int32_t serial,
            const ::aidl::android::hardware::radio::network::EmergencyNetworkScanTrigger& request)
            override;

    ::ndk::ScopedAStatus exitEmergencyMode(int32_t serial) override;

    ::ndk::ScopedAStatus cancelEmergencyNetworkScan(int32_t serial, bool resetScan) override;

    ::ndk::ScopedAStatus isN1ModeEnabled(int32_t serial) override;

    ::ndk::ScopedAStatus setN1ModeEnabled(int32_t serial, bool enable) override;

    ::ndk::ScopedAStatus setLocationPrivacySetting(int32_t serial, bool shareLocation) override;
    ::ndk::ScopedAStatus getLocationPrivacySetting(int32_t serial) override;

    ::ndk::ScopedAStatus setNullCipherAndIntegrityEnabled(int32_t serial, bool enabled) override;
};

}  // namespace cf::ril
