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

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

namespace cuttlefish {

class CvdVideoFrameBuffer : public VideoFrameBuffer {
 public:
  CvdVideoFrameBuffer(int width, int height);
  CvdVideoFrameBuffer(CvdVideoFrameBuffer&& cvd_frame_buf) = default;
  CvdVideoFrameBuffer(const CvdVideoFrameBuffer& cvd_frame_buf) = default;
  CvdVideoFrameBuffer& operator=(CvdVideoFrameBuffer&& cvd_frame_buf) = delete;
  CvdVideoFrameBuffer& operator=(const CvdVideoFrameBuffer& cvd_frame_buf) =
      delete;
  CvdVideoFrameBuffer() = delete;

  ~CvdVideoFrameBuffer() override;

  int width() const override;
  int height() const override;

  int StrideY() const override;
  int StrideU() const override;
  int StrideV() const override;

  uint8_t *DataY() override { return y_.data(); }
  uint8_t *DataU() override { return u_.data(); }
  uint8_t *DataV() override { return v_.data(); }

  size_t DataSizeY() const override { return y_.size(); }
  size_t DataSizeU() const override { return u_.size(); }
  size_t DataSizeV() const override { return v_.size(); }

 private:
  const int width_;
  const int height_;
  std::vector<uint8_t> y_;
  std::vector<uint8_t> u_;
  std::vector<uint8_t> v_;
};

}
