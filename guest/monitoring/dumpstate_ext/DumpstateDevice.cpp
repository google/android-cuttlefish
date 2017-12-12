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

#define LOG_TAG "dumpstate"

#include <log/log.h>
#include "DumpstateDevice.h"
#include "DumpstateUtil.h"

using android::os::dumpstate::DumpFileToFd;

namespace android {
namespace hardware {
namespace dumpstate {
namespace V1_0 {
namespace implementation {

// Methods from ::android::hardware::dumpstate::V1_0::IDumpstateDevice follow.
Return<void> DumpstateDevice::dumpstateBoard(const hidl_handle& handle) {
  if (handle == nullptr || handle->numFds < 1) {
    ALOGE("no FDs\n");
    return Void();
  }

  int fd = handle->data[0];
  if (fd < 0) {
    ALOGE("invalid FD: %d\n", handle->data[0]);
    return Void();
  }

  DumpFileToFd(fd, "GCE INITIAL METADATA", "/initial.metadata");

  return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace dumpstate
}  // namespace hardware
}  // namespace android

