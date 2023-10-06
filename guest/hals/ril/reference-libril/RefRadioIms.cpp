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

#include "RefRadioIms.h"

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

ScopedAStatus RefRadioIms::setSrvccCallInfo(
        int32_t serial,
        const std::vector<::aidl::android::hardware::radio::ims::SrvccCall>& srvccCalls) {
    respond()->setSrvccCallInfoResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioIms::updateImsRegistrationInfo(
        int32_t serial,
        const ::aidl::android::hardware::radio::ims::ImsRegistration& imsRegistration) {
    respond()->updateImsRegistrationInfoResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioIms::startImsTraffic(
        int32_t serial, int32_t token,
        ::aidl::android::hardware::radio::ims::ImsTrafficType imsTrafficType,
        ::aidl::android::hardware::radio::AccessNetwork accessNetworkType,
        ::aidl::android::hardware::radio::ims::ImsCall::Direction trafficDirection) {
    respond()->startImsTrafficResponse(responseInfo(serial), {});
    return ok();
}
ScopedAStatus RefRadioIms::stopImsTraffic(int32_t serial, int32_t token) {
    respond()->stopImsTrafficResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioIms::triggerEpsFallback(
        int32_t serial, ::aidl::android::hardware::radio::ims::EpsFallbackReason reason) {
    respond()->triggerEpsFallbackResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioIms::sendAnbrQuery(
        int32_t serial, ::aidl::android::hardware::radio::ims::ImsStreamType mediaType,
        ::aidl::android::hardware::radio::ims::ImsStreamDirection direction,
        int32_t bitsPerSecond) {
    respond()->sendAnbrQueryResponse(responseInfo(serial));
    return ok();
}
ScopedAStatus RefRadioIms::updateImsCallStatus(
        int32_t serial,
        const std::vector<::aidl::android::hardware::radio::ims::ImsCall>& imsCalls) {
    respond()->updateImsCallStatusResponse(responseInfo(serial));
    return ok();
}
}  // namespace cf::ril
