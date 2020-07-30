/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/frontend/webrtc/video_frame_buffer.h"

#include "common/libs/utils/size_utils.h"

namespace cuttlefish {

namespace {
constexpr int kPlanePadding = 1024;
constexpr int kLogAlignment = 6;  // multiple of 2^6

inline int AlignStride(int width) {
  return AlignToPowerOf2(width, kLogAlignment);
}

}  // namespace

CvdVideoFrameBuffer::CvdVideoFrameBuffer(int width, int height)
    : width_(width),
      height_(height),
      y_(AlignStride(width) * height + kPlanePadding),
      u_(AlignStride((width + 1) / 2) * ((height + 1) / 2) + kPlanePadding),
      v_(AlignStride((width + 1) / 2) * ((height + 1) / 2) + kPlanePadding) {}

// From VideoFrameBuffer
int CvdVideoFrameBuffer::width() const { return width_; }
int CvdVideoFrameBuffer::height() const { return height_; }

// From class PlanarYuvBuffer
int CvdVideoFrameBuffer::StrideY() const { return AlignStride(width_); }
int CvdVideoFrameBuffer::StrideU() const {
  return AlignStride((width_ + 1) / 2);
}
int CvdVideoFrameBuffer::StrideV() const {
  return AlignStride((width_ + 1) / 2);
}

// From class PlanarYuv8Buffer
const uint8_t *CvdVideoFrameBuffer::DataY() const { return y_.data(); }
const uint8_t *CvdVideoFrameBuffer::DataU() const { return u_.data(); }
const uint8_t *CvdVideoFrameBuffer::DataV() const { return v_.data(); }

}  // namespace cuttlefish
