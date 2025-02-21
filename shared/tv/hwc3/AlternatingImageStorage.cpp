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

#include "AlternatingImageStorage.h"

namespace aidl::android::hardware::graphics::composer3::impl {

uint8_t* AlternatingImageStorage::getRotatingScratchBuffer(
    std::size_t neededSize, std::uint32_t imageIndex) {
  std::size_t totalNeededSize = neededSize * kNumScratchBufferPieces;
  if (mScratchBuffer.size() < totalNeededSize) {
    mScratchBuffer.resize(totalNeededSize);
  }

  std::size_t bufferIndex = imageIndex % kNumScratchBufferPieces;
  std::size_t bufferOffset = bufferIndex * neededSize;
  return &mScratchBuffer[bufferOffset];
}

uint8_t* AlternatingImageStorage::getSpecialScratchBuffer(size_t neededSize) {
  if (mSpecialScratchBuffer.size() < neededSize) {
    mSpecialScratchBuffer.resize(neededSize);
  }

  return &mSpecialScratchBuffer[0];
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
