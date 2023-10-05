/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "RefImsMedia.h"

namespace cf::ril {

using ::ndk::ScopedAStatus;
using namespace ::aidl::android::hardware::radio::ims::media;
constexpr auto ok = &ScopedAStatus::ok;
std::shared_ptr<IImsMediaSessionListener> mediaSessionListener;

ScopedAStatus RefImsMediaSession::setListener(
        const std::shared_ptr<
                ::aidl::android::hardware::radio::ims::media::IImsMediaSessionListener>&
                in_sessionListener) {
    mediaSessionListener = in_sessionListener;
    return ok();
}

ScopedAStatus RefImsMediaSession::modifySession(
        const ::aidl::android::hardware::radio::ims::media::RtpConfig& in_config) {
    mediaSessionListener->onModifySessionResponse(
            in_config, ::aidl::android::hardware::radio::ims::media::RtpError::NONE);
    return ok();
}

ScopedAStatus RefImsMediaSession::sendDtmf(char16_t in_dtmfDigit, int32_t in_duration) {
    return ok();
}
ScopedAStatus RefImsMediaSession::startDtmf(char16_t in_dtmfDigit) {
    return ok();
}
ScopedAStatus RefImsMediaSession::stopDtmf() {
    return ok();
}
ScopedAStatus RefImsMediaSession::sendHeaderExtension(
        const std::vector<::aidl::android::hardware::radio::ims::media::RtpHeaderExtension>&
                in_extensions) {
    return ok();
}
ScopedAStatus RefImsMediaSession::setMediaQualityThreshold(
        const ::aidl::android::hardware::radio::ims::media::MediaQualityThreshold& in_threshold) {
    return ok();
}

}  // namespace cf::ril
