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

#ifndef ANDROID_HWC_COMMON_H
#define ANDROID_HWC_COMMON_H

#include <inttypes.h>

#include <string>

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#undef LOG_TAG
#define LOG_TAG "RanchuHwc"

#include <aidl/android/hardware/graphics/composer3/IComposerClient.h>
#include <android-base/logging.h>
#include <log/log.h>
#include <utils/Trace.h>

// Uncomment to enable additional debug logging.
// #define DEBUG_RANCHU_HWC

#if defined(DEBUG_RANCHU_HWC)
#define DEBUG_LOG ALOGE
#else
#define DEBUG_LOG(...) ((void)0)
#endif

namespace aidl::android::hardware::graphics::composer3::impl {

bool IsAutoDevice();
bool IsCuttlefish();
bool IsCuttlefishFoldable();

bool IsInNoOpCompositionMode();
bool IsInClientCompositionMode();

bool IsInGem5DisplayFinderMode();
bool IsInNoOpDisplayFinderMode();
bool IsInDrmDisplayFinderMode();

namespace HWC3 {
enum class Error : int32_t {
  None = 0,
  BadConfig = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_CONFIG,
  BadDisplay = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_DISPLAY,
  BadLayer = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_LAYER,
  BadParameter = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_PARAMETER,
  NoResources = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_NO_RESOURCES,
  NotValidated = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_NOT_VALIDATED,
  Unsupported = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_UNSUPPORTED,
  SeamlessNotAllowed = aidl::android::hardware::graphics::composer3::
      IComposerClient::EX_SEAMLESS_NOT_ALLOWED,
};
}  // namespace HWC3

std::string toString(HWC3::Error error);

inline ndk::ScopedAStatus ToBinderStatus(HWC3::Error error) {
  if (error != HWC3::Error::None) {
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(error));
  }
  return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
