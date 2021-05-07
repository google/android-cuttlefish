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

#include "host/frontend/webrtc/cvd_video_frame_buffer.h"

#include <map>
#include <mutex>
#include <vector>
#include "common/libs/utils/size_utils.h"

namespace cuttlefish {

namespace {
constexpr int kPlanePadding = 1024;
constexpr int kLogAlignment = 6;  // multiple of 2^6

inline int AlignStride(int width) {
  return AlignToPowerOf2(width, kLogAlignment);
}

std::multimap<int, std::vector<uint8_t>> pool;
std::mutex pool_mutex;
std::vector<uint8_t> FromPool(int size) {
  {
    std::lock_guard<std::mutex> lock(pool_mutex);
    auto it = pool.find(size);
    if (it != pool.end()) {
      auto ret = std::move(it->second);
      pool.erase(it);
      return ret;
    }
  }
  return std::vector<uint8_t>(size);
}

void BackToPool(std::vector<uint8_t> item) {
  std::lock_guard<std::mutex> lock(pool_mutex);
  pool.insert({item.size(), std::move(item)});
}

}  // namespace

CvdVideoFrameBuffer::CvdVideoFrameBuffer(int width, int height)
    : width_(width),
      height_(height),
      y_(FromPool(AlignStride(width) * height + kPlanePadding)),
      u_(FromPool(AlignStride((width + 1) / 2) * ((height + 1) / 2) +
                  kPlanePadding)),
      v_(FromPool(AlignStride((width + 1) / 2) * ((height + 1) / 2) +
                  kPlanePadding)) {}

CvdVideoFrameBuffer::~CvdVideoFrameBuffer() {
  BackToPool(std::move(y_));
  BackToPool(std::move(u_));
  BackToPool(std::move(v_));
}

int CvdVideoFrameBuffer::width() const { return width_; }
int CvdVideoFrameBuffer::height() const { return height_; }

int CvdVideoFrameBuffer::StrideY() const { return AlignStride(width_); }
int CvdVideoFrameBuffer::StrideU() const {
  return AlignStride((width_ + 1) / 2);
}
int CvdVideoFrameBuffer::StrideV() const {
  return AlignStride((width_ + 1) / 2);
}

const uint8_t *CvdVideoFrameBuffer::DataY() const { return y_.data(); }
const uint8_t *CvdVideoFrameBuffer::DataU() const { return u_.data(); }
const uint8_t *CvdVideoFrameBuffer::DataV() const { return v_.data(); }

}  // namespace cuttlefish
