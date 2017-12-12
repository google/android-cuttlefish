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
#ifndef GCE_DEVICE_PERSONALITY_IMPL_H_
#define GCE_DEVICE_PERSONALITY_IMPL_H_

#include <GceDevicePersonality.h>
#include <InitialMetadataReader.h>
#include <UniquePtr.h>

namespace avd {
class CameraConfiguration {
 public:
  CameraConfiguration(InitialMetadataReader* reader)
      : reader_(reader) {}

  ~CameraConfiguration() {}

  const std::vector<personality::Camera>& cameras() const {
    return cameras_;
  }

  void Init();

 private:
  std::vector<personality::Camera> cameras_;
};

}  // namespace avd

#endif  // GCE_DEVICE_PERSONALITY_IMPL_H_
