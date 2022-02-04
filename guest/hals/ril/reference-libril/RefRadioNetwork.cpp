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

#include "RefRadioNetwork.h"

namespace cf::ril {

using ::ndk::ScopedAStatus;
using namespace ::aidl::android::hardware::radio;
constexpr auto ok = &ScopedAStatus::ok;

static RadioResponseInfo responseInfo(int32_t serial, RadioError error = RadioError::NONE) {
    return {
            .type = RadioResponseType::SOLICITED,
            .serial = serial,
            .error = error,
    };
}

ScopedAStatus RefRadioNetwork::setUsageSetting(int32_t serial, network::UsageSetting usageSetting) {
    if (usageSetting != network::UsageSetting::VOICE_CENTRIC &&
        usageSetting != network::UsageSetting::DATA_CENTRIC) {
        respond()->setUsageSettingResponse(responseInfo(serial, RadioError::INVALID_ARGUMENTS));
        return ok();
    }

    mUsageSetting = usageSetting;
    respond()->setUsageSettingResponse(responseInfo(serial));
    return ok();
}

ScopedAStatus RefRadioNetwork::getUsageSetting(int32_t serial) {
    respond()->getUsageSettingResponse(responseInfo(serial), mUsageSetting);
    return ok();
}

}  // namespace cf::ril
