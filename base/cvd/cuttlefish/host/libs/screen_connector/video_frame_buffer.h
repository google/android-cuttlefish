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

class VideoFrameBuffer {
 public:
  virtual ~VideoFrameBuffer() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;

  // YUV Interfaces (Planar)
  virtual int StrideY() const { return 0; }
  virtual int StrideU() const { return 0; }
  virtual int StrideV() const { return 0; }
  virtual uint8_t* DataY() { return nullptr; }
  virtual uint8_t* DataU() { return nullptr; }
  virtual uint8_t* DataV() { return nullptr; }
  virtual size_t DataSizeY() const { return 0; }
  virtual size_t DataSizeU() const { return 0; }
  virtual size_t DataSizeV() const { return 0; }

  // Packed Interfaces (ABGR/ARGB)
  virtual uint8_t* Data() const { return nullptr; }
  virtual int Stride() const { return 0; }
  virtual size_t DataSize() const { return 0; }
  virtual uint32_t PixelFormat() const { return 0; }

  // Clone interface
  virtual std::unique_ptr<VideoFrameBuffer> Clone() const = 0;
};

}  // namespace cuttlefish
