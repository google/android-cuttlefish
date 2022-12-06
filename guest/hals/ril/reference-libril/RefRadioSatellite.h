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

#include <libradiocompat/RadioSatellite.h>

namespace cf::ril {

class RefRadioSatellite : public android::hardware::radio::compat::RadioSatellite {
  public:
    using android::hardware::radio::compat::RadioSatellite::RadioSatellite;

    ::ndk::ScopedAStatus getCapabilities(int32_t serial) override;
    ::ndk::ScopedAStatus setPower(int32_t serial, bool on) override;
    ::ndk::ScopedAStatus getPowerState(int32_t serial) override;
    ::ndk::ScopedAStatus provisionService(
            int32_t serial, const std::string& imei, const std::string& msisdn,
            const std::string& imsi,
            const std::vector<::aidl::android::hardware::radio::satellite::SatelliteFeature>&
                    features) override;
    ::ndk::ScopedAStatus addAllowedSatelliteContacts(
            int32_t serial, const std::vector<std::string>& contacts) override;
    ::ndk::ScopedAStatus removeAllowedSatelliteContacts(
            int32_t serial, const std::vector<std::string>& contacts) override;
    ::ndk::ScopedAStatus sendMessages(int32_t serial, const std::vector<std::string>& messages,
                                      const std::string& destination, double latitude,
                                      double longitude) override;
    ::ndk::ScopedAStatus getPendingMessages(int32_t serial) override;
    ::ndk::ScopedAStatus getSatelliteMode(int32_t serial) override;
    ::ndk::ScopedAStatus setIndicationFilter(int32_t serial, int32_t filterBitmask) override;
    ::ndk::ScopedAStatus startSendingSatellitePointingInfo(int32_t serial) override;
    ::ndk::ScopedAStatus stopSendingSatellitePointingInfo(int32_t serial) override;
    ::ndk::ScopedAStatus getMaxCharactersPerTextMessage(int32_t serial) override;
    ::ndk::ScopedAStatus getTimeForNextSatelliteVisibility(int32_t serial) override;
};

}  // namespace cf::ril
