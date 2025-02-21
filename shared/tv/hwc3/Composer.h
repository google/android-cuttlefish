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

#ifndef ANDROID_HWC_COMPOSER_H
#define ANDROID_HWC_COMPOSER_H

#include <aidl/android/hardware/graphics/composer3/BnComposer.h>
#include <aidl/android/hardware/tv/mediaquality/BnPictureProfileChangedListener.h>
#include <android-base/thread_annotations.h>

#include <memory>

#include "ComposerClient.h"
#include "PictureProfileChangedListener.h"

namespace aidl::android::hardware::graphics::composer3::impl {

// This class is basically just the interface to create a client.
class Composer : public BnComposer {
 public:
  Composer();

  binder_status_t dump(int fd, const char** args, uint32_t numArgs) override;

  // compser3 api
  ndk::ScopedAStatus createClient(
      std::shared_ptr<IComposerClient>* client) override;
  ndk::ScopedAStatus getCapabilities(std::vector<Capability>* caps) override;
  std::shared_ptr<PictureProfileChangedListener>
  getPictureProfileChangedListener();

 protected:
  ndk::SpAIBinder createBinder() override;

 private:
  bool waitForClientDestroyedLocked(std::unique_lock<std::mutex>& lock);
  void onClientDestroyed();

  std::mutex mClientMutex;
  std::weak_ptr<ComposerClient> mClient;
  std::condition_variable mClientDestroyedCondition;
  std::shared_ptr<PictureProfileChangedListener> mPictureProfileChangedListener;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
