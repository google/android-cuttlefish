/*
 * Copyright 2023 The Android Open Source Project
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

#ifndef ANDROID_HWC_ALTERNATINGIMAGESTORAGE_H
#define ANDROID_HWC_ALTERNATINGIMAGESTORAGE_H

#include <stdint.h>

#include <vector>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

// Provides storage for images when transforming images with the expectation
// that image N will no longer be used after producing image N + 1. With this,
// the storage just needs to be 2x the needed image size and the returned
// buffers can alternate back and forth.
class AlternatingImageStorage {
 public:
  AlternatingImageStorage() = default;

  uint8_t* getRotatingScratchBuffer(std::size_t neededSize,
                                    std::uint32_t imageIndex);

  uint8_t* getSpecialScratchBuffer(std::size_t neededSize);

 private:
  static constexpr const int kNumScratchBufferPieces = 2;

  // The main alternating storage.
  std::vector<uint8_t> mScratchBuffer;

  // Extra additional storage for one-off operations (scaling).
  std::vector<uint8_t> mSpecialScratchBuffer;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
