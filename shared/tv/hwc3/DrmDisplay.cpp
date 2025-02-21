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

#include "DrmDisplay.h"

#include "DrmAtomicRequest.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

template <typename T>
uint64_t addressAsUint(T* pointer) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

}  // namespace

std::unique_ptr<DrmDisplay> DrmDisplay::create(
    uint32_t id, std::unique_ptr<DrmConnector> connector,
    std::unique_ptr<DrmCrtc> crtc, std::unique_ptr<DrmPlane> plane,
    ::android::base::borrowed_fd drmFd) {
  if (!crtc) {
    ALOGE("%s: invalid crtc.", __FUNCTION__);
    return nullptr;
  }
  if (!connector) {
    ALOGE("%s: invalid connector.", __FUNCTION__);
    return nullptr;
  }
  if (!plane) {
    ALOGE("%s: invalid plane.", __FUNCTION__);
    return nullptr;
  }

  if (connector->isConnected()) {
    auto request = DrmAtomicRequest::create();
    if (!request) {
      ALOGE("%s: failed to create atomic request.", __FUNCTION__);
      return nullptr;
    }

    bool okay = true;
    okay &= request->Set(connector->getId(), connector->getCrtcProperty(),
                         crtc->getId());
    okay &= request->Set(crtc->getId(), crtc->getActiveProperty(), 1);
    okay &= request->Set(crtc->getId(), crtc->getModeProperty(),
                         connector->getDefaultMode()->getBlobId());
    okay &= request->Commit(drmFd);
    if (!okay) {
      ALOGE("%s: failed to set display mode.", __FUNCTION__);
      return nullptr;
    }
  }

  return std::unique_ptr<DrmDisplay>(new DrmDisplay(
      id, std::move(connector), std::move(crtc), std::move(plane)));
}

std::tuple<HWC3::Error, ::android::base::unique_fd> DrmDisplay::flush(
    ::android::base::borrowed_fd drmFd, ::android::base::borrowed_fd inSyncFd,
    const std::shared_ptr<DrmBuffer>& buffer) {
  std::unique_ptr<DrmAtomicRequest> request = DrmAtomicRequest::create();
  if (!request) {
    ALOGE("%s: failed to create atomic request.", __FUNCTION__);
    return std::make_tuple(HWC3::Error::NoResources,
                           ::android::base::unique_fd());
  }

  int flushFenceFd = -1;

  bool okay = true;
  okay &= request->Set(mCrtc->getId(), mCrtc->getOutFenceProperty(),
                       addressAsUint(&flushFenceFd));
  okay &=
      request->Set(mPlane->getId(), mPlane->getCrtcProperty(), mCrtc->getId());
  if (inSyncFd != -1) {
    okay &= request->Set(mPlane->getId(), mPlane->getInFenceProperty(),
                         static_cast<uint64_t>(inSyncFd.get()));
  }
  okay &= request->Set(mPlane->getId(), mPlane->getFbProperty(),
                       *buffer->mDrmFramebuffer);
  okay &= request->Set(mPlane->getId(), mPlane->getCrtcXProperty(), 0);
  okay &= request->Set(mPlane->getId(), mPlane->getCrtcYProperty(), 0);
  okay &=
      request->Set(mPlane->getId(), mPlane->getCrtcWProperty(), buffer->mWidth);
  okay &= request->Set(mPlane->getId(), mPlane->getCrtcHProperty(),
                       buffer->mHeight);
  okay &= request->Set(mPlane->getId(), mPlane->getSrcXProperty(), 0);
  okay &= request->Set(mPlane->getId(), mPlane->getSrcYProperty(), 0);
  okay &= request->Set(mPlane->getId(), mPlane->getSrcWProperty(),
                       buffer->mWidth << 16);
  okay &= request->Set(mPlane->getId(), mPlane->getSrcHProperty(),
                       buffer->mHeight << 16);

  okay &= request->Commit(drmFd);
  if (!okay) {
    ALOGE("%s: failed to flush to display.", __FUNCTION__);
    return std::make_tuple(HWC3::Error::NoResources,
                           ::android::base::unique_fd());
  }

  mPreviousBuffer = buffer;

  DEBUG_LOG("%s: submitted atomic update, flush fence:%d\n", __FUNCTION__,
            flushFenceFd);
  return std::make_tuple(HWC3::Error::None,
                         ::android::base::unique_fd(flushFenceFd));
}

bool DrmDisplay::onConnect(::android::base::borrowed_fd drmFd) {
  DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

  auto request = DrmAtomicRequest::create();
  if (!request) {
    ALOGE("%s: display:%" PRIu32 " failed to create atomic request.",
          __FUNCTION__, mId);
    return false;
  }

  bool okay = true;
  okay &= request->Set(mConnector->getId(), mConnector->getCrtcProperty(),
                       mCrtc->getId());
  okay &= request->Set(mCrtc->getId(), mCrtc->getActiveProperty(), 1);
  okay &= request->Set(mCrtc->getId(), mCrtc->getModeProperty(),
                       mConnector->getDefaultMode()->getBlobId());

  okay &= request->Commit(drmFd);
  if (!okay) {
    ALOGE("%s: display:%" PRIu32 " failed to set mode.", __FUNCTION__, mId);
    return false;
  }

  return true;
}

bool DrmDisplay::onDisconnect(::android::base::borrowed_fd drmFd) {
  DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

  auto request = DrmAtomicRequest::create();
  if (!request) {
    ALOGE("%s: display:%" PRIu32 " failed to create atomic request.",
          __FUNCTION__, mId);
    return false;
  }

  bool okay = true;
  okay &= request->Set(mPlane->getId(), mPlane->getCrtcProperty(), 0);
  okay &= request->Set(mPlane->getId(), mPlane->getFbProperty(), 0);

  okay &= request->Commit(drmFd);
  if (!okay) {
    ALOGE("%s: display:%" PRIu32 " failed to set mode", __FUNCTION__, mId);
  }

  mPreviousBuffer.reset();

  return okay;
}

DrmHotplugChange DrmDisplay::checkAndHandleHotplug(
    ::android::base::borrowed_fd drmFd) {
  DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

  const bool oldConnected = mConnector->isConnected();
  mConnector->update(drmFd);
  const bool newConnected = mConnector->isConnected();

  if (oldConnected == newConnected) {
    return DrmHotplugChange::kNoChange;
  }

  if (newConnected) {
    ALOGI("%s: display:%" PRIu32 " was connected.", __FUNCTION__, mId);
    if (!onConnect(drmFd)) {
      ALOGE("%s: display:%" PRIu32 " failed to connect.", __FUNCTION__, mId);
    }
    return DrmHotplugChange::kConnected;
  } else {
    ALOGI("%s: display:%" PRIu32 " was disconnected.", __FUNCTION__, mId);
    if (!onDisconnect(drmFd)) {
      ALOGE("%s: display:%" PRIu32 " failed to disconnect.", __FUNCTION__, mId);
    }
    return DrmHotplugChange::kDisconnected;
  }
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
