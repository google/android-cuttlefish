/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/frontend/webrtc/cvd_abgr_video_frame_buffer.h"

#include <string.h>

namespace cuttlefish {

CvdAbgrVideoFrameBuffer::CvdAbgrVideoFrameBuffer(int width, int height, 
                                                 uint32_t format, int stride,
                                                 const uint8_t* data)
    : width_(width),
      height_(height),
      format_(format),
      stride_(stride),
      data_(data, data + height * stride) {}

std::unique_ptr<VideoFrameBuffer> CvdAbgrVideoFrameBuffer::Clone() const {
  return std::make_unique<CvdAbgrVideoFrameBuffer>(*this);
}

}  // namespace cuttlefish
