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

#include "Composer.h"

#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

Composer::Composer() {
  if (PictureProfileChangedListener::isDeclared()) {
    mPictureProfileChangedListener =
        ndk::SharedRefBase::make<PictureProfileChangedListener>();
    if (!mPictureProfileChangedListener) {
      ALOGE("%s failed to allocate PictureProfileChangedListener",
            __FUNCTION__);
    }
  }
}

std::shared_ptr<PictureProfileChangedListener>
Composer::getPictureProfileChangedListener() {
  return mPictureProfileChangedListener;
}

ndk::ScopedAStatus Composer::createClient(
    std::shared_ptr<IComposerClient>* outClient) {
  DEBUG_LOG("%s", __FUNCTION__);

  std::unique_lock<std::mutex> lock(mClientMutex);

  const bool previousClientDestroyed = waitForClientDestroyedLocked(lock);
  if (!previousClientDestroyed) {
    ALOGE("%s: failed as composer client already exists", __FUNCTION__);
    *outClient = nullptr;
    return ToBinderStatus(HWC3::Error::NoResources);
  }

  auto client = ndk::SharedRefBase::make<ComposerClient>();
  if (!client) {
    ALOGE("%s: failed to init composer client", __FUNCTION__);
    *outClient = nullptr;
    return ToBinderStatus(HWC3::Error::NoResources);
  }

  auto error = client->init();
  if (error != HWC3::Error::None) {
    *outClient = nullptr;
    return ToBinderStatus(error);
  }

  auto clientDestroyed = [this]() { onClientDestroyed(); };
  client->setOnClientDestroyed(clientDestroyed);

  mClient = client;
  *outClient = client;
  client->setPictureProfileChangedListener(mPictureProfileChangedListener);

  return ndk::ScopedAStatus::ok();
}

bool Composer::waitForClientDestroyedLocked(
    std::unique_lock<std::mutex>& lock) {
  if (!mClient.expired()) {
    // In surface flinger we delete a composer client on one thread and
    // then create a new client on another thread. Although surface
    // flinger ensures the calls are made in that sequence (destroy and
    // then create), sometimes the calls land in the composer service
    // inverted (create and then destroy). Wait for a brief period to
    // see if the existing client is destroyed.
    constexpr const auto kTimeout = std::chrono::seconds(5);
    mClientDestroyedCondition.wait_for(
        lock, kTimeout, [this]() -> bool { return mClient.expired(); });
    if (!mClient.expired()) {
      ALOGW("%s: previous client was not destroyed", __FUNCTION__);
    }
  }

  return mClient.expired();
}

void Composer::onClientDestroyed() {
  std::lock_guard<std::mutex> lock(mClientMutex);

  mClientDestroyedCondition.notify_all();
}

binder_status_t Composer::dump(int fd, const char** /*args*/,
                               uint32_t /*numArgs*/) {
  DEBUG_LOG("%s", __FUNCTION__);

  std::string output("TODO");

  write(fd, output.c_str(), output.size());
  return STATUS_OK;
}

ndk::ScopedAStatus Composer::getCapabilities(std::vector<Capability>* caps) {
  DEBUG_LOG("%s", __FUNCTION__);

  caps->clear();
  caps->emplace_back(Capability::PRESENT_FENCE_IS_NOT_RELIABLE);
  caps->emplace_back(Capability::BOOT_DISPLAY_CONFIG);

  return ndk::ScopedAStatus::ok();
}

::ndk::SpAIBinder Composer::createBinder() {
  DEBUG_LOG("%s", __FUNCTION__);

  auto binder = BnComposer::createBinder();
  AIBinder_setInheritRt(binder.get(), true);
  return binder;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
