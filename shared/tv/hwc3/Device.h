/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ANDROID_HWC_DEVICE_H
#define ANDROID_HWC_DEVICE_H

#include <utils/Singleton.h>

#include <memory>
#include <thread>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class FrameComposer;

// Provides resources that are stable for the duration of the virtual
// device.
class Device : public ::android::Singleton<Device> {
 public:
  virtual ~Device() = default;

  HWC3::Error getComposer(FrameComposer** outComposer);

  bool persistentKeyValueEnabled() const;

  HWC3::Error getPersistentKeyValue(const std::string& key,
                                    const std::string& defaultVal,
                                    std::string* outValue);

  HWC3::Error setPersistentKeyValue(const std::string& key,
                                    const std::string& outValue);

 private:
  friend class Singleton<Device>;
  Device() = default;

  std::mutex mMutex;
  std::unique_ptr<FrameComposer> mComposer;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif