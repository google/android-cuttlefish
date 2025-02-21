/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef ANDROID_MEDIAQUALITY_MEDIAQUALITY_H
#define ANDROID_MEDIAQUALITY_MEDIAQUALITY_H

#include <aidl/android/hardware/tv/mediaquality/BnMediaQuality.h>
#include <aidl/android/hardware/tv/mediaquality/BnPictureProfileChangedListener.h>

#include <mutex>
#include <unordered_map>

namespace aidl::android::hardware::tv::mediaquality::impl {

namespace MEDIAQUALITY {
enum class Error : int32_t {
  None = 0,
  Unsupported = 1,
};
}  // namespace MEDIAQUALITY

class MediaQuality : public BnMediaQuality {
 public:
  MediaQuality();
  virtual ~MediaQuality();

  ndk::ScopedAStatus getPictureProfileListener(
      std::shared_ptr<::aidl::android::hardware::tv::mediaquality::
                          IPictureProfileChangedListener>*
          pictureProfileChangedListener) override;
  ndk::ScopedAStatus setAmbientBacklightDetector(
      const AmbientBacklightSettings& settings) override;
  ndk::ScopedAStatus setAmbientBacklightDetectionEnabled(bool enabled) override;
  ndk::ScopedAStatus getAmbientBacklightDetectionEnabled(
      bool* _aidl_return) override;
  ndk::ScopedAStatus isAutoPqSupported(bool* _aidl_return) override;
  ndk::ScopedAStatus getAutoPqEnabled(bool* _aidl_return) override;
  ndk::ScopedAStatus setAutoPqEnabled(bool enable) override;
  ndk::ScopedAStatus isAutoSrSupported(bool* _aidl_return) override;
  ndk::ScopedAStatus getAutoSrEnabled(bool* _aidl_return) override;
  ndk::ScopedAStatus setAutoSrEnabled(bool enable) override;
  ndk::ScopedAStatus isAutoAqSupported(bool* _aidl_return) override;
  ndk::ScopedAStatus getAutoAqEnabled(bool* _aidl_return) override;
  ndk::ScopedAStatus setAutoAqEnabled(bool enable) override;
  ndk::ScopedAStatus setPictureProfileAdjustmentListener(
      const std::shared_ptr<IPictureProfileAdjustmentListener>& listener)
      override;
  ndk::ScopedAStatus getSoundProfileListener(
      std::shared_ptr<ISoundProfileChangedListener>* _aidl_return) override;
  ndk::ScopedAStatus setSoundProfileAdjustmentListener(
      const std::shared_ptr<ISoundProfileAdjustmentListener>& listener)
      override;
  ndk::ScopedAStatus setAmbientBacklightCallback(
      const std::shared_ptr<
          ::aidl::android::hardware::tv::mediaquality::IMediaQualityCallback>&
          in_callback) override;
  ndk::ScopedAStatus sendDefaultPictureParameters(
      const ::aidl::android::hardware::tv::mediaquality::PictureParameters&
          in_pictureParameters) override;
  ndk::ScopedAStatus sendDefaultSoundParameters(
      const ::aidl::android::hardware::tv::mediaquality::SoundParameters&
          in_soundParameters) override;
  ndk::ScopedAStatus getParamCaps(
      const std::vector<
          ::aidl::android::hardware::tv::mediaquality::ParameterName>&
          in_paramNames,
      std::vector<::aidl::android::hardware::tv::mediaquality::ParamCapability>*
          out_caps) override;
  ndk::ScopedAStatus getVendorParamCaps(
      const std::vector<::aidl::android::hardware::tv::mediaquality::
                            VendorParameterIdentifier>& in_names,
      std::vector<
          ::aidl::android::hardware::tv::mediaquality::VendorParamCapability>*
          out_caps) override;

 protected:
  ndk::SpAIBinder createBinder() override;

 private:
  std::shared_ptr<IPictureProfileChangedListener>
      mPictureProfileChangedListener;
};

}  // namespace aidl::android::hardware::tv::mediaquality::impl

#endif
