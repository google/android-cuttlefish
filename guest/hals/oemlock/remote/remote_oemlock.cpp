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

#include "guest/hals/oemlock/remote/remote_oemlock.h"

#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace oemlock {
namespace {

enum {
    CUSTOM_ERROR_TRANSPORT_IS_FAILED = 0,
};

::ndk::ScopedAStatus resultToStatus(Result<void> r) {
    if (r.ok())
        return ::ndk::ScopedAStatus::ok();
    else
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                CUSTOM_ERROR_TRANSPORT_IS_FAILED, r.error().Message().c_str());
}

}

OemLock::OemLock(secure_env::Channel& channel) : channel_(channel) {}

::ndk::ScopedAStatus OemLock::getName(std::string *out_name) {
    *out_name = "CF Remote Implementation";
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus OemLock::setOemUnlockAllowedByCarrier(bool in_allowed,
                                                           const std::vector<uint8_t> &,
                                                           OemLockSecureStatus *_aidl_return) {
    *_aidl_return = OemLockSecureStatus::OK;
    return resultToStatus(setValue(secure_env::OemLockField::ALLOWED_BY_CARRIER, in_allowed));
}

::ndk::ScopedAStatus OemLock::isOemUnlockAllowedByCarrier(bool *out_allowed) {
    return resultToStatus(requestValue(secure_env::OemLockField::ALLOWED_BY_CARRIER, out_allowed));
}

::ndk::ScopedAStatus OemLock::setOemUnlockAllowedByDevice(bool in_allowed) {
    return resultToStatus(setValue(secure_env::OemLockField::ALLOWED_BY_DEVICE, in_allowed));
}

::ndk::ScopedAStatus OemLock::isOemUnlockAllowedByDevice(bool *out_allowed) {
    return resultToStatus(requestValue(secure_env::OemLockField::ALLOWED_BY_DEVICE, out_allowed));
}

Result<void> OemLock::requestValue(secure_env::OemLockField field, bool *out) {
    CF_EXPECT(channel_.SendRequest(static_cast<uint32_t>(field), nullptr, 0),
              "Can't send get value request for field: " << static_cast<uint32_t>(field));
    auto response = CF_EXPECT(channel_.ReceiveMessage(),
                              "Haven't received an answer for getting the field: " <<
                              static_cast<uint32_t>(field));
    *out = *reinterpret_cast<bool*>(response->payload);
    return {};
}

Result<void> OemLock::setValue(secure_env::OemLockField field, bool value) {
    CF_EXPECT(channel_.SendRequest(static_cast<uint32_t>(field), &value, sizeof(bool)),
              "Can't send set value request for field: " << static_cast<uint32_t>(field));
    auto response = CF_EXPECT(channel_.ReceiveMessage(),
                              "Haven't received an answer for setting the field: " <<
                              static_cast<uint32_t>(field));
    auto updated_value = *reinterpret_cast<bool*>(response->payload);
    CF_EXPECT(value == updated_value,
              "Updated value for the field " << static_cast<uint32_t>(field) <<
              " is different from what we wated to set");
    return {};
}

} // namespace oemlock
} // namespace hardware
} // namespace android
} // aidl
