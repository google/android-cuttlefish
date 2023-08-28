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
#pragma once

#include <libradiocompat/RadioImsMedia.h>
#include <libradiocompat/RadioImsMediaSession.h>

namespace cf::ril {

class RefImsMedia : public android::hardware::radio::compat::RadioImsMedia {
  public:
    using android::hardware::radio::compat::RadioImsMedia::RadioImsMedia;

    ::ndk::ScopedAStatus setListener(
            const std::shared_ptr<::aidl::android::hardware::radio::ims::media::IImsMediaListener>&
                    in_mediaListener) override;
    ::ndk::ScopedAStatus openSession(
            int32_t in_sessionId,
            const ::aidl::android::hardware::radio::ims::media::LocalEndPoint& in_localEndPoint,
            const ::aidl::android::hardware::radio::ims::media::RtpConfig& in_config) override;
    ::ndk::ScopedAStatus closeSession(int32_t in_sessionId) override;
};

class RefImsMediaSession : public android::hardware::radio::compat::RadioImsMediaSession {
  public:
    using android::hardware::radio::compat::RadioImsMediaSession::RadioImsMediaSession;

    ::ndk::ScopedAStatus setListener(
            const std::shared_ptr<
                    ::aidl::android::hardware::radio::ims::media::IImsMediaSessionListener>&
                    in_sessionListener) override;
    ::ndk::ScopedAStatus modifySession(
            const ::aidl::android::hardware::radio::ims::media::RtpConfig& in_config) override;
    ::ndk::ScopedAStatus sendDtmf(char16_t in_dtmfDigit, int32_t in_duration) override;
    ::ndk::ScopedAStatus startDtmf(char16_t in_dtmfDigit) override;
    ::ndk::ScopedAStatus stopDtmf() override;
    ::ndk::ScopedAStatus sendHeaderExtension(
            const std::vector<::aidl::android::hardware::radio::ims::media::RtpHeaderExtension>&
                    in_extensions) override;
    ::ndk::ScopedAStatus setMediaQualityThreshold(
            const ::aidl::android::hardware::radio::ims::media::MediaQualityThreshold& in_threshold)
            override;
};

}  // namespace cf::ril
