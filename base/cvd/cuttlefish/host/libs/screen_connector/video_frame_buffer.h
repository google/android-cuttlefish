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

#include <memory>

namespace cuttlefish {

// Base class for packed video frame buffers (ABGR, ARGB, etc.).
class VideoFrameBuffer {
 public:
  virtual ~VideoFrameBuffer() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;

  virtual uint8_t* Data() const = 0;
  virtual int Stride() const = 0;
  virtual size_t DataSize() const = 0;

  // DRM_FORMAT_* fourcc
  virtual uint32_t PixelFormat() const = 0;

  virtual std::unique_ptr<VideoFrameBuffer> Clone() const = 0;
};

}  // namespace cuttlefish
