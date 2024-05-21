/*
 * Copyright 2021 The Android Open Source Project
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
#define LOG_TAG "VsockCameraProvider"
#include "vsock_camera_provider_2_7.h"
#include <cutils/properties.h>
#include <log/log.h>
#include "vsock_camera_server.h"

namespace android::hardware::camera::provider::V2_7::implementation {

namespace {
VsockCameraServer gCameraServer;
constexpr auto kDeviceName = "device@3.4/external/0";
}  // namespace

using android::hardware::camera::provider::V2_7::ICameraProvider;
extern "C" ICameraProvider* HIDL_FETCH_ICameraProvider(const char* name) {
  return (strcmp(name, "external/0") == 0)
             ? new VsockCameraProvider(&gCameraServer)
             : nullptr;
}

VsockCameraProvider::VsockCameraProvider(VsockCameraServer* server) {
  server_ = server;
  if (!server->isRunning()) {
    constexpr static const auto camera_port_property =
        "ro.boot.vsock_camera_port";
    auto port = property_get_int32(camera_port_property, -1);
    if (port > 0) {
      server->start(port, VMADDR_CID_ANY);
    }
  }
}

VsockCameraProvider::~VsockCameraProvider() {
  server_->setConnectedCallback(nullptr);
}

Return<Status> VsockCameraProvider::setCallback(
    const sp<ICameraProviderCallback>& callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_ = callback;
  }
  server_->setConnectedCallback(
      [this](std::shared_ptr<cuttlefish::VsockConnection> connection,
             VsockCameraDevice::Settings settings) {
        connection_ = connection;
        settings_ = settings;
        deviceAdded(kDeviceName);
        connection_->SetDisconnectCallback(
            [this] { deviceRemoved(kDeviceName); });
      });
  return Status::OK;
}

Return<void> VsockCameraProvider::getVendorTags(
    ICameraProvider::getVendorTags_cb _hidl_cb) {
  // No vendor tag support
  hidl_vec<VendorTagSection> empty;
  _hidl_cb(Status::OK, empty);
  return Void();
}

Return<void> VsockCameraProvider::getCameraIdList(
    ICameraProvider::getCameraIdList_cb _hidl_cb) {
  // External camera HAL always report 0 camera, and extra cameras
  // are just reported via cameraDeviceStatusChange callbacks
  hidl_vec<hidl_string> empty;
  _hidl_cb(Status::OK, empty);
  return Void();
}

Return<void> VsockCameraProvider::isSetTorchModeSupported(
    ICameraProvider::isSetTorchModeSupported_cb _hidl_cb) {
  // setTorchMode API is supported, though right now no external camera device
  // has a flash unit.
  _hidl_cb(Status::OK, true);
  return Void();
}

Return<void> VsockCameraProvider::getCameraDeviceInterface_V1_x(
    const hidl_string&,
    ICameraProvider::getCameraDeviceInterface_V1_x_cb _hidl_cb) {
  // External Camera HAL does not support HAL1
  _hidl_cb(Status::OPERATION_NOT_SUPPORTED, nullptr);
  return Void();
}

Return<void> VsockCameraProvider::getCameraDeviceInterface_V3_x(
    const hidl_string& name_hidl_str,
    ICameraProvider::getCameraDeviceInterface_V3_x_cb _hidl_cb) {
  std::string name(name_hidl_str.c_str());
  if (name != kDeviceName) {
    _hidl_cb(Status::ILLEGAL_ARGUMENT, nullptr);
    return Void();
  }

  _hidl_cb(Status::OK, new VsockCameraDevice(name, settings_, connection_));
  return Void();
}

Return<void> VsockCameraProvider::notifyDeviceStateChange(
    hardware::hidl_bitfield<DeviceState> /*newState*/) {
  return Void();
}

Return<void> VsockCameraProvider::getConcurrentStreamingCameraIds(
    getConcurrentStreamingCameraIds_cb _hidl_cb) {
  hidl_vec<hidl_vec<hidl_string>> hidl_camera_id_combinations;
  _hidl_cb(Status::OK, hidl_camera_id_combinations);
  return Void();
}

Return<void> VsockCameraProvider::isConcurrentStreamCombinationSupported(
    const hidl_vec<::android::hardware::camera::provider::V2_6::CameraIdAndStreamCombination>&,
    isConcurrentStreamCombinationSupported_cb _hidl_cb) {
  _hidl_cb(Status::OK, false);
  return Void();
}

Return<void> VsockCameraProvider::isConcurrentStreamCombinationSupported_2_7(
    const hidl_vec<::android::hardware::camera::provider::V2_7::CameraIdAndStreamCombination>&,
    isConcurrentStreamCombinationSupported_2_7_cb _hidl_cb) {
  _hidl_cb(Status::OK, false);
  return Void();
}

void VsockCameraProvider::deviceAdded(const char* name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (callbacks_ != nullptr) {
    callbacks_->cameraDeviceStatusChange(name, CameraDeviceStatus::PRESENT);
  }
}

void VsockCameraProvider::deviceRemoved(const char* name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (callbacks_ != nullptr) {
    callbacks_->cameraDeviceStatusChange(name, CameraDeviceStatus::NOT_PRESENT);
  }
}

}  // namespace android::hardware::camera::provider::V2_7::implementation
