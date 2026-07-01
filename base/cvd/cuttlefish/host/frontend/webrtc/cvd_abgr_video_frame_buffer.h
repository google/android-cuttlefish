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

#pragma once

#include <stdint.h>
#include <vector>

#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

namespace cuttlefish {

class CvdAbgrVideoFrameBuffer : public PackedVideoFrameBuffer {
 public:
  CvdAbgrVideoFrameBuffer(int width, int height, uint32_t format, int stride, 
                          const uint8_t* data);
  ~CvdAbgrVideoFrameBuffer() override = default;

  int width() const override { return width_; }
  int height() const override { return height_; }
  
  uint8_t* Data() const override { return const_cast<uint8_t*>(data_.data()); }
  int Stride() const override { return stride_; }
  std::size_t DataSize() const override { return data_.size(); }
  uint32_t PixelFormat() const override { return format_; }

  std::unique_ptr<VideoFrameBuffer> Clone() const override;

 private:
  int width_;
  int height_;
  uint32_t format_;
  int stride_;
  std::vector<uint8_t> data_;
};

}  // namespace cuttlefish
