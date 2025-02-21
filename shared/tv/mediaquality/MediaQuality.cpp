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

#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>
#include <android/binder_manager.h>
#include <android/binder_status.h>
#include <log/log.h>

#include "MediaQuality.h"

namespace aidl::android::hardware::tv::mediaquality::impl {

MediaQuality::MediaQuality() : mPictureProfileChangedListener(nullptr) {
  mPictureProfileChangedListener = IPictureProfileChangedListener::fromBinder(
      ndk::SpAIBinder(AServiceManager_waitForService(
          (std::string(IPictureProfileChangedListener::descriptor) + "/default")
              .c_str())));
  if (!mPictureProfileChangedListener) {
    LOG_ALWAYS_FATAL("Failed to get PictureProfileChangedListener");
    return;
  }
  ALOGI("Successfully fetched IPictureProfileChangedListener");
}

MediaQuality::~MediaQuality() {}

ndk::ScopedAStatus MediaQuality::getPictureProfileListener(
    std::shared_ptr<::aidl::android::hardware::tv::mediaquality::
                        IPictureProfileChangedListener>*
        pictureProfileChangedListener) {
  *pictureProfileChangedListener = mPictureProfileChangedListener;
  return ::ndk::ScopedAStatus::ok();
}

::ndk::SpAIBinder MediaQuality::createBinder() {
  auto binder = BnMediaQuality::createBinder();
  AIBinder_setInheritRt(binder.get(), true);
  return binder;
}

::ndk::ScopedAStatus MediaQuality::setAmbientBacklightDetector(
    const AmbientBacklightSettings& /* settings */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::setAmbientBacklightDetectionEnabled(
    bool /* enabled */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::getAmbientBacklightDetectionEnabled(
    bool* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::isAutoPqSupported(bool* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::getAutoPqEnabled(bool* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::setAutoPqEnabled(bool /* enable */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::isAutoSrSupported(bool* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::getAutoSrEnabled(bool* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::setAutoSrEnabled(bool /* enable */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::isAutoAqSupported(bool* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::getAutoAqEnabled(bool* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::setAutoAqEnabled(bool /* enable */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::setPictureProfileAdjustmentListener(
    const std::shared_ptr<IPictureProfileAdjustmentListener>& /* listener */) {
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MediaQuality::getSoundProfileListener(
    std::shared_ptr<ISoundProfileChangedListener>* /* _aidl_return */) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::setSoundProfileAdjustmentListener(
    const std::shared_ptr<ISoundProfileAdjustmentListener>& /* listener */) {
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MediaQuality::setAmbientBacklightCallback(
    const std::shared_ptr<
        ::aidl::android::hardware::tv::mediaquality::IMediaQualityCallback>&
        in_callback) {
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MediaQuality::sendDefaultPictureParameters(
    const ::aidl::android::hardware::tv::mediaquality::PictureParameters&
        in_pictureParameters) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::sendDefaultSoundParameters(
    const ::aidl::android::hardware::tv::mediaquality::SoundParameters&
        in_soundParameters) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::getParamCaps(
    const std::vector<
        ::aidl::android::hardware::tv::mediaquality::ParameterName>&
        in_paramNames,
    std::vector<::aidl::android::hardware::tv::mediaquality::ParamCapability>*
        out_caps) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

::ndk::ScopedAStatus MediaQuality::getVendorParamCaps(
    const std::vector<
        ::aidl::android::hardware::tv::mediaquality::VendorParameterIdentifier>&
        in_names,
    std::vector<
        ::aidl::android::hardware::tv::mediaquality::VendorParamCapability>*
        out_caps) {
  return ndk::ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(MEDIAQUALITY::Error::Unsupported));
}

}  // namespace aidl::android::hardware::tv::mediaquality::impl
