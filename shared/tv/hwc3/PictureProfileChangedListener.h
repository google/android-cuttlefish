#ifndef ANDROID_HWC3_PICTUREPROFILECHANGEDLISTENER_H
#define ANDROID_HWC3_PICTUREPROFILECHANGEDLISTENER_H

#include <aidl/android/hardware/tv/mediaquality/BnPictureProfileChangedListener.h>
#include <android-base/thread_annotations.h>
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

using aidl::android::hardware::tv::mediaquality::
    BnPictureProfileChangedListener;
using aidl::android::hardware::tv::mediaquality::PictureParameter;
using aidl::android::hardware::tv::mediaquality::PictureParameters;
using aidl::android::hardware::tv::mediaquality::PictureProfile;

class PictureProfileChangedListener : public BnPictureProfileChangedListener {
 public:
  PictureProfileChangedListener();
  virtual ~PictureProfileChangedListener();

  static bool isDeclared();
  bool applyProfile(int64_t id, Layer* layer);
  ndk::ScopedAStatus onPictureProfileChanged(
      const PictureProfile& pictureProfile) override;

 protected:
  ndk::SpAIBinder createBinder() override;

 private:
  void updatePictureProfile(const PictureProfile& pictureProfile);
  std::mutex mPictureProfilesMutex;  // Mutex for thread safety
  std::unordered_map<int64_t, PictureProfile> mPictureProfiles
      GUARDED_BY(mPictureProfilesMutex);
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
