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

#include "cuttlefish/host/libs/gpu/rgba_to_nv12.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>

namespace cuttlefish {
namespace {

uint8_t ClampToByte(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

struct Yuv {
  int y;
  int u;
  int v;
};

// BT.709 limited range, matching the color config in the sequence header.
Yuv RgbToYuv(int r, int g, int b) {
  return Yuv{
      .y = ((46 * r + 157 * g + 16 * b + 128) >> 8) + 16,
      .u = ((-26 * r - 87 * g + 113 * b + 128) >> 8) + 128,
      .v = ((112 * r - 102 * g - 10 * b + 128) >> 8) + 128,
  };
}

}  // namespace

void ConvertRgbaToNv12(const uint8_t* source,
                       const Nv12ConversionParams& params,
                       uint8_t* destination) {
  uint8_t* luma = destination;
  uint8_t* chroma = destination + static_cast<size_t>(params.coded_width) *
                                      params.coded_height;
  const size_t source_stride =
      static_cast<size_t>(params.source_stride_pixels) * kRgbaBytesPerPixel;
  const int red_offset = static_cast<int>(params.red_offset);
  const int blue_offset = 2 - red_offset;

  for (uint32_t y = 0; y < params.coded_height; y += 2) {
    for (uint32_t x = 0; x < params.coded_width; x += 2) {
      int u_sum = 0;
      int v_sum = 0;
      for (uint32_t dy = 0; dy < 2; dy++) {
        for (uint32_t dx = 0; dx < 2; dx++) {
          const uint32_t source_x = std::min(x + dx, params.visible_width - 1);
          const uint32_t source_y = std::min(y + dy, params.visible_height - 1);
          const uint8_t* pixel =
              source + source_y * source_stride + source_x * kRgbaBytesPerPixel;
          const Yuv yuv =
              RgbToYuv(pixel[red_offset], pixel[1], pixel[blue_offset]);
          luma[(y + dy) * params.coded_width + x + dx] = ClampToByte(yuv.y);
          u_sum += yuv.u;
          v_sum += yuv.v;
        }
      }
      chroma[(y / 2) * params.coded_width + x] = ClampToByte(u_sum / 4);
      chroma[(y / 2) * params.coded_width + x + 1] = ClampToByte(v_sum / 4);
    }
  }
}

uint64_t Nv12FrameSize(uint32_t coded_width, uint32_t coded_height) {
  return static_cast<uint64_t>(coded_width) * coded_height * 3 / 2;
}

}  // namespace cuttlefish
