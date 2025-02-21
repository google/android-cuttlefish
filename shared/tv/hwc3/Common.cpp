/*
 * Copyright 2022 The Android Open Source Project
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

#include "Common.h"

#include <android-base/properties.h>

namespace aidl::android::hardware::graphics::composer3::impl {

bool IsAutoDevice() {
  // gcar_emu_x86_64, sdk_car_md_x86_64, cf_x86_64_auto, cf_x86_64_only_auto_md
  const std::string product_name =
      ::android::base::GetProperty("ro.product.name", "");
  return product_name.find("car_") != std::string::npos ||
         product_name.find("_auto") != std::string::npos;
}

bool IsCuttlefish() {
  return ::android::base::GetProperty("ro.product.board", "") == "cutf";
}

bool IsCuttlefishFoldable() {
  return IsCuttlefish() &&
         ::android::base::GetProperty("ro.product.name", "").find("foldable") !=
             std::string::npos;
}

bool IsInNoOpCompositionMode() {
  const std::string mode =
      ::android::base::GetProperty("ro.vendor.hwcomposer.mode", "");
  DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.mode is %s", __FUNCTION__,
            mode.c_str());
  return mode == "noop";
}

bool IsInClientCompositionMode() {
  const std::string mode =
      ::android::base::GetProperty("ro.vendor.hwcomposer.mode", "");
  DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.mode is %s", __FUNCTION__,
            mode.c_str());
  return mode == "client";
}

bool IsInGem5DisplayFinderMode() {
  const std::string mode = ::android::base::GetProperty(
      "ro.vendor.hwcomposer.display_finder_mode", "");
  DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.display_finder_mode is %s",
            __FUNCTION__, mode.c_str());
  return mode == "gem5";
}

bool IsInNoOpDisplayFinderMode() {
  const std::string mode = ::android::base::GetProperty(
      "ro.vendor.hwcomposer.display_finder_mode", "");
  DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.display_finder_mode is %s",
            __FUNCTION__, mode.c_str());
  return mode == "noop";
}

bool IsInDrmDisplayFinderMode() {
  const std::string mode = ::android::base::GetProperty(
      "ro.vendor.hwcomposer.display_finder_mode", "");
  DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.display_finder_mode is %s",
            __FUNCTION__, mode.c_str());
  return mode == "drm";
}

std::string toString(HWC3::Error error) {
  switch (error) {
    case HWC3::Error::None:
      return "None";
    case HWC3::Error::BadConfig:
      return "BadConfig";
    case HWC3::Error::BadDisplay:
      return "BadDisplay";
    case HWC3::Error::BadLayer:
      return "BadLayer";
    case HWC3::Error::BadParameter:
      return "BadParameter";
    case HWC3::Error::NoResources:
      return "NoResources";
    case HWC3::Error::NotValidated:
      return "NotValidated";
    case HWC3::Error::Unsupported:
      return "Unsupported";
    case HWC3::Error::SeamlessNotAllowed:
      return "SeamlessNotAllowed";
  }
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
