/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <android/hardware/dumpstate/1.1/IDumpstateDevice.h>
#include <android/hardware/dumpstate/1.1/types.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace dumpstate {
namespace V1_1 {
namespace implementation {

using ::android::sp;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::dumpstate::V1_1::DumpstateMode;
using ::android::hardware::dumpstate::V1_1::DumpstateStatus;
using ::android::hardware::dumpstate::V1_1::IDumpstateDevice;

struct DumpstateDevice : public IDumpstateDevice {
  // Methods from ::android::hardware::dumpstate::V1_0::IDumpstateDevice follow.
  Return<void> dumpstateBoard(const hidl_handle& h) override;

  // Methods from ::android::hardware::dumpstate::V1_1::IDumpstateDevice follow.
  Return<DumpstateStatus> dumpstateBoard_1_1(const hidl_handle& h,
                                             DumpstateMode mode,
                                             uint64_t timeoutMillis) override;
  Return<void> setVerboseLoggingEnabled(bool enable) override;
  Return<bool> getVerboseLoggingEnabled() override;
};

}  // namespace implementation
}  // namespace V1_1
}  // namespace dumpstate
}  // namespace hardware
}  // namespace android
