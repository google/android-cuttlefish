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
#include "guest/monitoring/dumpstate_ext/dumpstate_device.h"

#include <DumpstateUtil.h>
#include <android-base/properties.h>
#include <log/log.h>

#define VERBOSE_LOGGING_PROPERTY "persist.vendor.verbose_logging_enabled"

using android::os::dumpstate::DumpFileToFd;

namespace android {
namespace hardware {
namespace dumpstate {
namespace V1_1 {
namespace implementation {

// Methods from ::android::hardware::dumpstate::V1_0::IDumpstateDevice follow.
Return<void> DumpstateDevice::dumpstateBoard(const hidl_handle& handle) {
  // Ignore return value, just return an empty status.
  dumpstateBoard_1_1(handle, DumpstateMode::DEFAULT, 30 * 1000 /* timeoutMillis */);
  return Void();
}

// Methods from ::android::hardware::dumpstate::V1_1::IDumpstateDevice follow.
Return<DumpstateStatus> DumpstateDevice::dumpstateBoard_1_1(const hidl_handle& handle,
                                                            DumpstateMode mode,
                                                            uint64_t /* timeoutMillis */) {
  if (handle == nullptr || handle->numFds < 1) {
    ALOGE("No FDs\n");
    return DumpstateStatus::ILLEGAL_ARGUMENT;
  }

  int fd = handle->data[0];
  if (fd < 0) {
    ALOGE("Invalid FD: %d\n", fd);
    return DumpstateStatus::ILLEGAL_ARGUMENT;
  }

  bool isModeValid = false;
  for (const auto dumpstateMode : hidl_enum_range<DumpstateMode>()) {
    isModeValid |= (dumpstateMode == mode);
  }
  if (!isModeValid) {
    ALOGE("Invalid mode: %d\n", mode);
    return DumpstateStatus::ILLEGAL_ARGUMENT;
  }

  if (mode == DumpstateMode::WEAR) {
    // We aren't a Wear device. Mostly just for variety in our return values for testing purposes.
    ALOGE("Unsupported mode: %d\n", mode);
    return DumpstateStatus::UNSUPPORTED_MODE;
  }

  if (mode == DumpstateMode::PROTO) {
    // We don't support dumping a protobuf yet.
    ALOGE("Unsupported mode: %d\n", mode);
    return DumpstateStatus::UNSUPPORTED_MODE;
  }

  DumpFileToFd(fd, "GCE INITIAL METADATA", "/initial.metadata");

  // Do not include any of the user's private information before checking if verbose logging is
  // enabled.
  return DumpstateStatus::OK;
}

Return<void> DumpstateDevice::setVerboseLoggingEnabled(bool enable) {
  ::android::base::SetProperty(VERBOSE_LOGGING_PROPERTY, enable ? "true" : "false");
  return Void();
}

Return<bool> DumpstateDevice::getVerboseLoggingEnabled() {
  return ::android::base::GetBoolProperty(VERBOSE_LOGGING_PROPERTY, false);
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace dumpstate
}  // namespace hardware
}  // namespace android
