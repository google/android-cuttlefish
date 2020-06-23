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
#ifndef GUEST_HALS_CAMERA_CAMERACONFIGURATION_H_
#define GUEST_HALS_CAMERA_CAMERACONFIGURATION_H_

#undef max
#undef min
#include <algorithm>
#include <vector>

namespace cuttlefish {

// Camera properties and features.
struct CameraDefinition {
  // Camera facing direction.
  enum Orientation { kFront, kBack };

  // Camera recognized HAL versions.
  enum HalVersion { kHalV1, kHalV2, kHalV3 };

  struct Resolution {
    int width;
    int height;
  };

  Orientation orientation;
  HalVersion hal_version;
  std::vector<Resolution> resolutions;
};

class CameraConfiguration {
 public:
  CameraConfiguration() {}
  ~CameraConfiguration() {}

  const std::vector<CameraDefinition>& cameras() const { return cameras_; }

  bool Init();

 private:
  std::vector<CameraDefinition> cameras_;
};

}  // namespace cuttlefish

#endif  // GUEST_HALS_CAMERA_CAMERACONFIGURATION_H_
