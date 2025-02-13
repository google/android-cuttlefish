#include "PictureProfileChangedListener.h"
#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>
#include <android/binder_manager.h>

namespace aidl::android::hardware::graphics::composer3::impl {

PictureProfileChangedListener::PictureProfileChangedListener()
    : mPictureProfilesMutex(), mPictureProfiles() {}

PictureProfileChangedListener::~PictureProfileChangedListener() {}

void PictureProfileChangedListener::updatePictureProfile(
    const PictureProfile& pictureProfile) {
  std::lock_guard<std::mutex> lock(mPictureProfilesMutex);
  mPictureProfiles[pictureProfile.pictureProfileId] = pictureProfile;
}

bool PictureProfileChangedListener::applyProfile(int64_t id, Layer* layer) {
  std::lock_guard<std::mutex> lock(mPictureProfilesMutex);
  if (mPictureProfiles.find(id) == mPictureProfiles.end()) {
    return false;
  }
  PictureParameters pictureParameters = mPictureProfiles[id].parameters;
  for (const auto& parameter : pictureParameters.pictureParameters) {
    if (parameter.getTag() == PictureParameter::Tag::brightness) {
      layer->setBrightness(parameter.get<PictureParameter::Tag::brightness>());
    }
  }
  return true;
}

ndk::ScopedAStatus PictureProfileChangedListener::onPictureProfileChanged(
    const PictureProfile& pictureProfile) {
  updatePictureProfile(pictureProfile);
  return ndk::ScopedAStatus::ok();
}

ndk::SpAIBinder PictureProfileChangedListener::createBinder() {
  auto binder = BnPictureProfileChangedListener::createBinder();
  AIBinder_setInheritRt(binder.get(), true);
  return binder;
}

bool PictureProfileChangedListener::isDeclared() {
  const std::string instance =
      std::string() + PictureProfileChangedListener::descriptor + "/default";
  return AServiceManager_isDeclared(instance.c_str());
}
}  // namespace aidl::android::hardware::graphics::composer3::impl
