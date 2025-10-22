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

#define LOG_TAG "SchedulingService"

#include "SchedulingService.h"

#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace npu {

ndk::ScopedAStatus checkPriorities(std::vector<SchedulingConfig> configs) {
  for (SchedulingConfig& config : configs) {
    if (config.priority < SchedulingConfig::MIN_PRIORITY ||
        config.priority > SchedulingConfig::MAX_PRIORITY) {
      return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
          EX_ILLEGAL_ARGUMENT,
          "Invalid priority. Must be between SchedulingConfig::LOWEST_PRIORITY "
          "and SchedulingConfig::HIGHEST_PRIORITY");
    }
  }

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SchedulingService::setSchedulingConfigs(
    const std::vector<SchedulingConfig>& schedulingConfigs) {
  LOG(INFO) << "setSchedulingConfigs received " << schedulingConfigs.size()
            << " configs";
  ndk::ScopedAStatus checkStatus = checkPriorities(schedulingConfigs);
  if (!checkStatus.isOk()) {
    return checkStatus;
  }

  mSchedulingConfigs.clear();
  updateSchedulingConfigs(schedulingConfigs);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SchedulingService::updateSchedulingConfigs(
    const std::vector<SchedulingConfig>& schedulingConfigs) {
  LOG(INFO) << "updateSchedulingConfigs received " << schedulingConfigs.size()
            << " configs";
  ndk::ScopedAStatus checkStatus = checkPriorities(schedulingConfigs);
  if (!checkStatus.isOk()) {
    return checkStatus;
  }

  for (const SchedulingConfig& config : schedulingConfigs) {
    mSchedulingConfigs[config.uid] = config;
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SchedulingService::setCallback(
    const std::shared_ptr<ISchedulingCallback>& callback) {
  LOG(INFO) << "setCallback called";
  mCallback = callback;
  return ndk::ScopedAStatus::ok();
}

}  // namespace npu
}  // namespace hardware
}  // namespace android
}  // namespace aidl
