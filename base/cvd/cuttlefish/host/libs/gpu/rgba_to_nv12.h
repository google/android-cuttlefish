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

namespace cuttlefish {

// Bytes per pixel of the packed RGBA frames the encoder takes.
constexpr uint32_t kRgbaBytesPerPixel = 4;

// Frame geometry shared by the host converter and the shader. A source
// coordinate beyond the visible frame repeats the edge pixel, which keeps the
// padding the coded alignment adds cheap to encode.
struct Nv12ConversionParams {
  uint32_t visible_width = 0;
  uint32_t visible_height = 0;
  uint32_t coded_width = 0;
  uint32_t coded_height = 0;
  // Distance between source rows, in pixels.
  uint32_t source_stride_pixels = 0;
  // Byte offset of red within a pixel: 0 where blue comes last, 2 where it
  // comes first.
  uint32_t red_offset = 0;
};

// Converts packed RGBA to NV12 (BT.709 limited range) on the host. Writes the
// luma plane followed by the interleaved chroma plane, both at the coded size.
// The shader produces the same bytes.
void ConvertRgbaToNv12(const uint8_t* source,
                       const Nv12ConversionParams& params,
                       uint8_t* destination);

// Returns the size in bytes of an NV12 frame at this coded size.
uint64_t Nv12FrameSize(uint32_t coded_width, uint32_t coded_height);

}  // namespace cuttlefish
