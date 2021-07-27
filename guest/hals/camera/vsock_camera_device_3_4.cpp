/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define LOG_TAG "VsockCameraDevice"
//#define LOG_NDEBUG 0
#include <log/log.h>

#include <algorithm>
#include <array>
#include "CameraMetadata.h"
#include "android-base/macros.h"
#include "include/convert.h"
#include "vsock_camera_device_3_4.h"
#include "vsock_camera_device_session_3_4.h"

namespace android::hardware::camera::device::V3_4::implementation {

VsockCameraDevice::VsockCameraDevice(
    const std::string& id, const Settings& settings,
    std::shared_ptr<cuttlefish::VsockConnection> connection)
    : id_(id),
      metadata_(settings.width, settings.height, settings.frame_rate),
      connection_(connection),
      is_open_(false) {
  ALOGI("%s", __FUNCTION__);
}

VsockCameraDevice::~VsockCameraDevice() { ALOGI("%s", __FUNCTION__); }

Return<void> VsockCameraDevice::getResourceCost(
    ICameraDevice::getResourceCost_cb _hidl_cb) {
  CameraResourceCost resCost;
  resCost.resourceCost = 100;
  _hidl_cb(Status::OK, resCost);
  return Void();
}

Return<void> VsockCameraDevice::getCameraCharacteristics(
    ICameraDevice::getCameraCharacteristics_cb _hidl_cb) {
  V3_2::CameraMetadata hidl_vec;
  const camera_metadata_t* metadata_ptr = metadata_.getAndLock();
  V3_2::implementation::convertToHidl(metadata_ptr, &hidl_vec);
  _hidl_cb(Status::OK, hidl_vec);
  metadata_.unlock(metadata_ptr);
  return Void();
}

Return<Status> VsockCameraDevice::setTorchMode(TorchMode) {
  return Status::OPERATION_NOT_SUPPORTED;
}

Return<void> VsockCameraDevice::open(const sp<ICameraDeviceCallback>& callback,
                                     ICameraDevice::open_cb _hidl_cb) {
  if (callback == nullptr) {
    ALOGE("%s: cannot open camera %s. callback is null!", __FUNCTION__,
          id_.c_str());
    _hidl_cb(Status::ILLEGAL_ARGUMENT, nullptr);
    return Void();
  }

  bool was_open = is_open_.exchange(true);

  if (was_open) {
    ALOGE("%s: cannot open an already opened camera!", __FUNCTION__);
    _hidl_cb(Status::CAMERA_IN_USE, nullptr);
    return Void();
  }
  ALOGI("%s: Initializing device for camera %s", __FUNCTION__, id_.c_str());
  frame_provider_ = std::make_shared<cuttlefish::VsockFrameProvider>();
  frame_provider_->start(connection_, metadata_.getPreferredWidth(),
                         metadata_.getPreferredHeight());
  session_ = new VsockCameraDeviceSession(metadata_, frame_provider_, callback);
  _hidl_cb(Status::OK, session_);
  return Void();
}

Return<void> VsockCameraDevice::dumpState(
    const ::android::hardware::hidl_handle& handle) {
  if (handle.getNativeHandle() == nullptr) {
    ALOGE("%s: handle must not be null", __FUNCTION__);
    return Void();
  }
  if (handle->numFds != 1 || handle->numInts != 0) {
    ALOGE("%s: handle must contain 1 FD and 0 integers! Got %d FDs and %d ints",
          __FUNCTION__, handle->numFds, handle->numInts);
    return Void();
  }
  int fd = handle->data[0];
  dprintf(fd, "Camera:%s\n", id_.c_str());
  return Void();
}

}  // namespace android::hardware::camera::device::V3_4::implementation
