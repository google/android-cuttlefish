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
std::shared_ptr<IImsMediaListener> mediaListener;

ScopedAStatus RefImsMedia::setListener(
        const std::shared_ptr<::aidl::android::hardware::radio::ims::media::IImsMediaListener>&
                in_mediaListener) {
    mediaListener = in_mediaListener;
    return ok();
}
ScopedAStatus RefImsMedia::openSession(
        int32_t in_sessionId,
        const ::aidl::android::hardware::radio::ims::media::LocalEndPoint& in_localEndPoint,
        const ::aidl::android::hardware::radio::ims::media::RtpConfig& in_config) {
    std::shared_ptr<IImsMediaSession> session =
            ndk::SharedRefBase::make<RefImsMediaSession>(mContext, mHal1_5, mCallbackManager);

    mediaListener->onOpenSessionSuccess(in_sessionId, session);
    return ok();
}
ScopedAStatus RefImsMedia::closeSession(int32_t in_sessionId) {
    mediaListener->onSessionClosed(in_sessionId);
    return ok();
}

}  // namespace cf::ril
