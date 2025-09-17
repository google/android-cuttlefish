// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <aidl/android/hardware/npu/BnScheduling.h>
#include <aidl/android/hardware/npu/ISchedulingCallback.h>
#include <aidl/android/hardware/npu/SchedulingConfig.h>
#include <map>
#include <memory>

namespace aidl {
namespace android {
namespace hardware {
namespace npu {

class SchedulingService : public BnScheduling {
 public:
  ndk::ScopedAStatus setSchedulingConfigs(
      const std::vector<SchedulingConfig>& uidSchedulingConfigs) override;
  ndk::ScopedAStatus updateSchedulingConfigs(
      const std::vector<SchedulingConfig>& uidSchedulingConfigs) override;
  ndk::ScopedAStatus setCallback(
      const std::shared_ptr<ISchedulingCallback>& callback) override;

 private:
  std::shared_ptr<ISchedulingCallback> mCallback;

  std::map<int, SchedulingConfig> mSchedulingConfigs;
};

}  // namespace npu
}  // namespace hardware
}  // namespace android
}  // namespace aidl
