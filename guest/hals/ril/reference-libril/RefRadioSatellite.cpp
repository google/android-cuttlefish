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

#include "RefRadioSatellite.h"

namespace cf::ril {

using ::ndk::ScopedAStatus;
using namespace ::aidl::android::hardware::radio;
constexpr auto ok = &ScopedAStatus::ok;

static RadioResponseInfo responseInfo(int32_t serial) {
    return {
            .type = RadioResponseType::SOLICITED,
            .serial = serial,
            .error = RadioError::NONE,
    };
}

ScopedAStatus RefRadioSatellite::getCapabilities(int32_t serial) {
    satellite::SatelliteCapabilities capabilities;
    respond()->getCapabilitiesResponse(responseInfo(serial), capabilities);
    return ok();
}
ScopedAStatus RefRadioSatellite::setPower(int32_t serial, bool on) {
    respond()->setPowerResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioSatellite::getPowerState(int32_t serial) {
    respond()->getPowerStateResponse(responseInfo(serial), true);
    return ok();
}
ScopedAStatus RefRadioSatellite::provisionService(
        int32_t serial, const std::string& imei, const std::string& msisdn, const std::string& imsi,
        const std::vector<::aidl::android::hardware::radio::satellite::SatelliteFeature>&
                features) {
    respond()->provisionServiceResponse(responseInfo(serial), true);
    return ok();
}
ScopedAStatus RefRadioSatellite::addAllowedSatelliteContacts(
        int32_t serial, const std::vector<std::string>& contacts) {
    respond()->addAllowedSatelliteContactsResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioSatellite::removeAllowedSatelliteContacts(
        int32_t serial, const std::vector<std::string>& contacts) {
    respond()->removeAllowedSatelliteContactsResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioSatellite::sendMessages(int32_t serial,
                                              const std::vector<std::string>& messages,
                                              const std::string& destination, double latitude,
                                              double longitude) {
    respond()->sendMessagesResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioSatellite::getPendingMessages(int32_t serial) {
    std::vector<std::string> messages = {"This is a test message."};
    respond()->getPendingMessagesResponse(responseInfo(serial), messages);
    return ok();
}
ScopedAStatus RefRadioSatellite::getSatelliteMode(int32_t serial) {
    satellite::SatelliteMode mode = satellite::SatelliteMode::ACQUIRED;
    satellite::NTRadioTechnology radioTechnology = satellite::NTRadioTechnology::NB_IOT_NTN;
    respond()->getSatelliteModeResponse(responseInfo(serial), mode, radioTechnology);
    return ok();
}
ScopedAStatus RefRadioSatellite::setIndicationFilter(int32_t serial, int32_t filterBitmask) {
    respond()->setIndicationFilterResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioSatellite::startSendingSatellitePointingInfo(int32_t serial) {
    respond()->startSendingSatellitePointingInfoResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioSatellite::stopSendingSatellitePointingInfo(int32_t serial) {
    respond()->stopSendingSatellitePointingInfoResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioSatellite::getMaxCharactersPerTextMessage(int32_t serial) {
    respond()->getMaxCharactersPerTextMessageResponse(responseInfo(serial), 100);
    return ok();
}
ScopedAStatus RefRadioSatellite::getTimeForNextSatelliteVisibility(int32_t serial) {
    respond()->getTimeForNextSatelliteVisibilityResponse(responseInfo(serial), 10);
    return ok();
}
}  // namespace cf::ril
