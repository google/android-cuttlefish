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
#pragma once

#include "CameraMetadata.h"

#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include "vsock_camera_device_session_3_4.h"
#include "vsock_camera_metadata.h"
#include "vsock_connection.h"
#include "vsock_frame_provider.h"

#include <vector>

namespace android::hardware::camera::device::V3_4::implementation {

using namespace ::android::hardware::camera::device;
using ::android::sp;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::camera::common::V1_0::CameraResourceCost;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::common::V1_0::TorchMode;
using ::android::hardware::camera::device::V3_2::ICameraDevice;
using ::android::hardware::camera::device::V3_2::ICameraDeviceCallback;

class VsockCameraDevice : public ICameraDevice {
 public:
  using Settings = struct {
    int32_t width;
    int32_t height;
    double frame_rate;
  };

  VsockCameraDevice(const std::string& id, const Settings& settings,
                    std::shared_ptr<cuttlefish::VsockConnection> connection);
  virtual ~VsockCameraDevice();

  /* Methods from ::android::hardware::camera::device::V3_2::ICameraDevice
   * follow. */
  Return<void> getResourceCost(ICameraDevice::getResourceCost_cb _hidl_cb);
  Return<void> getCameraCharacteristics(
      ICameraDevice::getCameraCharacteristics_cb _hidl_cb);
  Return<Status> setTorchMode(TorchMode);
  Return<void> open(const sp<ICameraDeviceCallback>&, ICameraDevice::open_cb);
  Return<void> dumpState(const ::android::hardware::hidl_handle&);
  /* End of Methods from
   * ::android::hardware::camera::device::V3_2::ICameraDevice */

 private:
  std::string id_;
  VsockCameraMetadata metadata_;
  std::shared_ptr<cuttlefish::VsockConnection> connection_;
  std::shared_ptr<cuttlefish::VsockFrameProvider> frame_provider_;
  std::atomic<bool> is_open_;
  sp<VsockCameraDeviceSession> session_;
};

}  // namespace android::hardware::camera::device::V3_4::implementation
