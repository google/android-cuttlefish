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

#include <libradiocompat/RadioIms.h>

namespace cf::ril {

class RefRadioIms : public android::hardware::radio::compat::RadioIms {
  public:
    using android::hardware::radio::compat::RadioIms::RadioIms;

    ::ndk::ScopedAStatus setSrvccCallInfo(
            int32_t serial,
            const std::vector<::aidl::android::hardware::radio::ims::SrvccCall>& srvccCalls)
            override;
    ::ndk::ScopedAStatus updateImsRegistrationInfo(
            int32_t serial,
            const ::aidl::android::hardware::radio::ims::ImsRegistration& imsRegistration) override;
    ::ndk::ScopedAStatus startImsTraffic(
            int32_t serial, int32_t token,
            ::aidl::android::hardware::radio::ims::ImsTrafficType imsTrafficType,
            ::aidl::android::hardware::radio::AccessNetwork accessNetworkType,
            ::aidl::android::hardware::radio::ims::ImsCall::Direction trafficDirection) override;
    ::ndk::ScopedAStatus stopImsTraffic(int32_t serial, int32_t token) override;
    ::ndk::ScopedAStatus triggerEpsFallback(
            int32_t serial,
            ::aidl::android::hardware::radio::ims::EpsFallbackReason reason) override;
    ::ndk::ScopedAStatus sendAnbrQuery(
            int32_t serial, ::aidl::android::hardware::radio::ims::ImsStreamType mediaType,
            ::aidl::android::hardware::radio::ims::ImsStreamDirection direction,
            int32_t bitsPerSecond) override;
    ::ndk::ScopedAStatus updateImsCallStatus(
            int32_t serial,
            const std::vector<::aidl::android::hardware::radio::ims::ImsCall>& imsCalls) override;
};

}  // namespace cf::ril
