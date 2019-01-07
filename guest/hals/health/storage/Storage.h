/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef ANDROID_HARDWARE_HEALTH_FILESYSTEM_V1_0_FILESYSTEM_H
#define ANDROID_HARDWARE_HEALTH_FILESYSTEM_V1_0_FILESYSTEM_H

#include <android/hardware/health/storage/1.0/IStorage.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace health {
namespace storage {
namespace V1_0 {
namespace implementation {

using ::android::sp;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;

struct Storage : public IStorage {
    Return<void> garbageCollect(uint64_t timeoutSeconds,
                                const sp<IGarbageCollectCallback>& cb) override;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace storage
}  // namespace health
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_HEALTH_FILESYSTEM_V1_0_FILESYSTEM_H
