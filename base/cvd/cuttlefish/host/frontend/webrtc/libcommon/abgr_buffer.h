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

#include <api/video/video_frame_buffer.h>
#include <memory>

#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

namespace cuttlefish {

class AbgrBuffer : public webrtc::VideoFrameBuffer {
 public:
  explicit AbgrBuffer(std::shared_ptr<cuttlefish::VideoFrameBuffer> buffer);
  
  Type type() const override { return Type::kNative; }
  int width() const override;
  int height() const override;
  
  rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
  
  const uint8_t* Data() const;
  int Stride() const;
  uint32_t PixelFormat() const { return buffer_->PixelFormat(); }

 private:
  std::shared_ptr<cuttlefish::VideoFrameBuffer> buffer_;
};

}  // namespace cuttlefish
