/*
 * Copyright (C) 2016 The Android Open Source Project
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
class GceDevicePersonalityImpl : public GceDevicePersonality {
 public:
  GceDevicePersonalityImpl(InitialMetadataReader* reader)
      : reader_(reader) {}

  ~GceDevicePersonalityImpl() {}

  virtual const std::vector<personality::Camera>& cameras() {
    return cameras_;
  }

  void Init();

 private:
  // Clear all properties.
  void Reset();

  // Initialize from supplied JSON object.
  bool InitFromJsonObject(const std::string& json_object);

  // Initialize from supplied device name.
  // Will attempt to find configuration for this device in device personalities
  // path.
  bool InitFromPersonalityName(const std::string& device_name);

  // Fallback to old settings. If not present, initialize with sane defaults.
  bool InitFromLegacySettings();

  InitialMetadataReader* reader_;
  std::vector<personality::Camera> cameras_;
};

}  // namespace avd

#endif  // GCE_DEVICE_PERSONALITY_IMPL_H_
