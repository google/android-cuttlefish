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

#include <aidl/android/hardware/oemlock/BnOemLock.h>

#include "common/libs/security/channel.h"
#include "common/libs/security/oemlock.h"
#include "common/libs/utils/result.h"

namespace aidl {
namespace android {
namespace hardware {
namespace oemlock {

using namespace cuttlefish;

struct OemLock : public BnOemLock {
public:
    OemLock(secure_env::Channel& channel);

    // Methods from ::android::hardware::oemlock::IOemLock follow.
    ::ndk::ScopedAStatus getName(std::string* out_name) override;
    ::ndk::ScopedAStatus isOemUnlockAllowedByCarrier(bool* out_allowed) override;
    ::ndk::ScopedAStatus isOemUnlockAllowedByDevice(bool* out_allowed) override;
    ::ndk::ScopedAStatus setOemUnlockAllowedByCarrier(bool in_allowed,
                                                      const std::vector<uint8_t>&,
                                                      OemLockSecureStatus* _aidl_return) override;
    ::ndk::ScopedAStatus setOemUnlockAllowedByDevice(bool in_allowed) override;

private:
    secure_env::Channel& channel_;

    Result<void> requestValue(secure_env::OemLockField field, bool *out);
    Result<void> setValue(secure_env::OemLockField field, bool value);
};

} // namespace oemlock
} // namespace hardware
} // namespace android
} // aidl
