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

#include <mutex>

#include <android/hardware/camera/provider/2.7/ICameraProvider.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <json/json.h>

#include "vsock_camera_device_3_4.h"
#include "vsock_camera_server.h"
#include "vsock_connection.h"

namespace android::hardware::camera::provider::V2_7::implementation {

using ::android::sp;
using ::android::hardware::hidl_string;
using ::android::hardware::Return;
using ::android::hardware::camera::common::V1_0::CameraDeviceStatus;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::common::V1_0::VendorTagSection;
using ::android::hardware::camera::device::V3_4::implementation::
    VsockCameraDevice;
using ::android::hardware::camera::provider::V2_4::ICameraProviderCallback;
using ::android::hardware::camera::provider::V2_5::DeviceState;
using ::android::hardware::camera::provider::V2_7::ICameraProvider;

class VsockCameraProvider : public ICameraProvider {
 public:
  VsockCameraProvider(VsockCameraServer* server);
  ~VsockCameraProvider();

  Return<Status> setCallback(
      const sp<ICameraProviderCallback>& callback) override;
  Return<void> getVendorTags(getVendorTags_cb _hidl_cb) override;
  Return<void> getCameraIdList(getCameraIdList_cb _hidl_cb) override;
  Return<void> isSetTorchModeSupported(
      isSetTorchModeSupported_cb _hidl_cb) override;
  Return<void> getCameraDeviceInterface_V1_x(
      const hidl_string& cameraDeviceName,
      getCameraDeviceInterface_V1_x_cb _hidl_cb) override;
  Return<void> getCameraDeviceInterface_V3_x(
      const hidl_string& cameraDeviceName,
      getCameraDeviceInterface_V3_x_cb _hidl_cb) override;
  Return<void> notifyDeviceStateChange(
      hardware::hidl_bitfield<DeviceState> newState) override;
  Return<void> getConcurrentStreamingCameraIds(
      getConcurrentStreamingCameraIds_cb _hidl_cb) override;
  Return<void> isConcurrentStreamCombinationSupported(
      const hidl_vec<::android::hardware::camera::provider::V2_6::CameraIdAndStreamCombination>& configs,
      isConcurrentStreamCombinationSupported_cb _hidl_cb) override;
  Return<void> isConcurrentStreamCombinationSupported_2_7(
      const hidl_vec<::android::hardware::camera::provider::V2_7::CameraIdAndStreamCombination>& configs,
      isConcurrentStreamCombinationSupported_2_7_cb _hidl_cb) override;

 private:
  void deviceRemoved(const char* name);
  void deviceAdded(const char* name);
  std::mutex mutex_;
  sp<ICameraProviderCallback> callbacks_;
  std::shared_ptr<cuttlefish::VsockConnection> connection_;
  VsockCameraDevice::Settings settings_;
  VsockCameraServer* server_;
};

}  // namespace android::hardware::camera::provider::V2_7::implementation
