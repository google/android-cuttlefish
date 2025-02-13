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

#ifndef ANDROID_HWC_FENCEDBUFFER_H
#define ANDROID_HWC_FENCEDBUFFER_H

#include <aidlcommonsupport/NativeHandle.h>
#include <android-base/unique_fd.h>
#include <cutils/native_handle.h>

namespace aidl::android::hardware::graphics::composer3::impl {

class FencedBuffer {
 public:
  FencedBuffer() : mBuffer(nullptr) {}

  void set(buffer_handle_t buffer, const ndk::ScopedFileDescriptor& fence) {
    mBuffer = buffer;
    mFence = GetUniqueFd(fence);
  }

  buffer_handle_t getBuffer() const { return mBuffer; }

  ::android::base::unique_fd getFence() const {
    if (mFence.ok()) {
      return ::android::base::unique_fd(dup(mFence.get()));
    } else {
      return ::android::base::unique_fd();
    }
  }

 private:
  static ::android::base::unique_fd GetUniqueFd(
      const ndk::ScopedFileDescriptor& in) {
    auto& sfd = const_cast<ndk::ScopedFileDescriptor&>(in);
    ::android::base::unique_fd ret(sfd.get());
    *sfd.getR() = -1;
    return ret;
  }

  buffer_handle_t mBuffer;
  ::android::base::unique_fd mFence;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
