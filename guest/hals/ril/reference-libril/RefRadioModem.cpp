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

#include "RefRadioModem.h"
#include "ril_service.h"

using ::android::hardware::hidl_string;

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

    hidl_string convertCharPtrToHidlString(const char *ptr) {
        hidl_string ret;
        if (ptr != NULL) {
            ret.setToExternal(ptr, strlen(ptr));
        }
        return ret;
    }

    ScopedAStatus RefRadioModem::getImei(int32_t serial) {
        ::aidl::android::hardware::radio::modem::ImeiInfo imeiInfo = {};
        imeiInfo.type = (::aidl::android::hardware::radio::modem::ImeiInfo::ImeiType) 1;
        imeiInfo.imei = convertCharPtrToHidlString("867400022047199");
        imeiInfo.svn = convertCharPtrToHidlString("01");
        respond()->getImeiResponse(responseInfo(serial), imeiInfo);
        return ok();
    }
}