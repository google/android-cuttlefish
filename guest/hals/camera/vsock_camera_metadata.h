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

#include <CameraMetadata.h>
#include <android/hardware/camera/device/3.2/ICameraDevice.h>

namespace android::hardware::camera::device::V3_4::implementation {

// Small wrappers for mostly hard-coded camera metadata
// Some parameters are calculated from remote camera frame size and fps
using ::android::hardware::camera::common::V1_0::helper::CameraMetadata;
class VsockCameraMetadata : public CameraMetadata {
 public:
  VsockCameraMetadata(int32_t width, int32_t height, int32_t fps);

  int32_t getPreferredWidth() const { return width_; }
  int32_t getPreferredHeight() const { return height_; }
  int32_t getPreferredFps() const { return fps_; }

 private:
  int32_t width_;
  int32_t height_;
  int32_t fps_;
};

using ::android::hardware::camera::device::V3_2::RequestTemplate;
class VsockCameraRequestMetadata : public CameraMetadata {
 public:
  VsockCameraRequestMetadata(int32_t fps, RequestTemplate type);
  // Tells whether the metadata has been successfully constructed
  // from the parameters
  bool isValid() const { return is_valid_; }

 private:
  bool is_valid_;
};

}  // namespace android::hardware::camera::device::V3_4::implementation
