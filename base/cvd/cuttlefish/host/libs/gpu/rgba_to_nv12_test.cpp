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

#include <vector>

#include "gtest/gtest.h"

namespace cuttlefish {
namespace {

// A frame of one color, with a stride that may exceed the visible width.
std::vector<uint8_t> SolidFrame(uint32_t width, uint32_t height,
                                uint32_t stride_pixels, uint8_t r, uint8_t g,
                                uint8_t b) {
  std::vector<uint8_t> frame(static_cast<size_t>(stride_pixels) * height * 4,
                             0);
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint8_t* pixel =
          frame.data() + (static_cast<size_t>(y) * stride_pixels + x) * 4;
      pixel[0] = r;
      pixel[1] = g;
      pixel[2] = b;
      pixel[3] = 255;
    }
  }
  return frame;
}

Nv12ConversionParams Params(uint32_t width, uint32_t height,
                            uint32_t coded_width, uint32_t coded_height,
                            uint32_t stride_pixels) {
  return Nv12ConversionParams{
      .visible_width = width,
      .visible_height = height,
      .coded_width = coded_width,
      .coded_height = coded_height,
      .source_stride_pixels = stride_pixels,
      .red_offset = 0,
  };
}

TEST(Nv12FrameSizeTest, IsOneAndAHalfPlanes) {
  EXPECT_EQ(Nv12FrameSize(1920, 1088), 1920u * 1088u * 3 / 2);
  EXPECT_EQ(Nv12FrameSize(2, 2), 6u);
}

TEST(ConvertRgbaToNv12Test, WhiteIsLimitedRangePeak) {
  const std::vector<uint8_t> white = SolidFrame(2, 2, 2, 255, 255, 255);
  std::vector<uint8_t> nv12(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(white.data(), Params(2, 2, 2, 2, 2), nv12.data());
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(nv12[i], 234) << "luma " << i;
  }
  EXPECT_EQ(nv12[4], 128);
  EXPECT_EQ(nv12[5], 128);
}

TEST(ConvertRgbaToNv12Test, BlackIsLimitedRangeFloor) {
  const std::vector<uint8_t> black = SolidFrame(2, 2, 2, 0, 0, 0);
  std::vector<uint8_t> nv12(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(black.data(), Params(2, 2, 2, 2, 2), nv12.data());
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(nv12[i], 16) << "luma " << i;
  }
  EXPECT_EQ(nv12[4], 128);
  EXPECT_EQ(nv12[5], 128);
}

TEST(ConvertRgbaToNv12Test, RedTakesTheBt709Coefficients) {
  const std::vector<uint8_t> red = SolidFrame(2, 2, 2, 255, 0, 0);
  std::vector<uint8_t> nv12(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(red.data(), Params(2, 2, 2, 2, 2), nv12.data());
  EXPECT_EQ(nv12[0], 62);
  EXPECT_EQ(nv12[4], 102);
  EXPECT_EQ(nv12[5], 240);
}

TEST(ConvertRgbaToNv12Test, RedOffsetSwapsRedAndBlue) {
  // The same bytes read as ARGB, where blue comes first, have to give what
  // blue gives when read as ABGR.
  const std::vector<uint8_t> pixels = SolidFrame(2, 2, 2, 255, 0, 0);
  Nv12ConversionParams params = Params(2, 2, 2, 2, 2);
  params.red_offset = 2;

  std::vector<uint8_t> nv12(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(pixels.data(), params, nv12.data());

  const std::vector<uint8_t> blue = SolidFrame(2, 2, 2, 0, 0, 255);
  std::vector<uint8_t> reference(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(blue.data(), Params(2, 2, 2, 2, 2), reference.data());
  EXPECT_EQ(nv12, reference);
}

TEST(ConvertRgbaToNv12Test, PaddingRepeatsTheEdgePixel) {
  // A 2x2 frame coded as 4x4: every padded position repeats the pixel it sits
  // beyond, so the whole plane holds the same value.
  const std::vector<uint8_t> frame = SolidFrame(2, 2, 2, 12, 34, 56);
  std::vector<uint8_t> nv12(Nv12FrameSize(4, 4));
  ConvertRgbaToNv12(frame.data(), Params(2, 2, 4, 4, 2), nv12.data());

  for (size_t i = 0; i < 16; i++) {
    EXPECT_EQ(nv12[i], nv12[0]) << "luma " << i;
  }
  for (size_t i = 16; i < nv12.size(); i += 2) {
    EXPECT_EQ(nv12[i], nv12[16]) << "chroma u " << i;
    EXPECT_EQ(nv12[i + 1], nv12[17]) << "chroma v " << i;
  }
}

TEST(ConvertRgbaToNv12Test, StrideSkipsThePaddingBetweenRows) {
  // Rows four pixels apart in a frame two pixels wide. The bytes past the
  // visible width are left at zero and must not reach the output.
  const std::vector<uint8_t> padded = SolidFrame(2, 2, 4, 200, 100, 50);
  std::vector<uint8_t> from_padded(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(padded.data(), Params(2, 2, 2, 2, 4), from_padded.data());

  const std::vector<uint8_t> packed = SolidFrame(2, 2, 2, 200, 100, 50);
  std::vector<uint8_t> from_packed(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(packed.data(), Params(2, 2, 2, 2, 2), from_packed.data());

  EXPECT_EQ(from_padded, from_packed);
}

TEST(ConvertRgbaToNv12Test, ChromaAveragesTheBlock) {
  // Two black and two white pixels in one block average to the value both
  // colors share, and the luma keeps them apart.
  std::vector<uint8_t> frame(2 * 2 * 4, 0);
  for (int i = 0; i < 2; i++) {
    uint8_t* pixel = frame.data() + i * 4;
    pixel[0] = 255;
    pixel[1] = 255;
    pixel[2] = 255;
    pixel[3] = 255;
  }

  std::vector<uint8_t> nv12(Nv12FrameSize(2, 2));
  ConvertRgbaToNv12(frame.data(), Params(2, 2, 2, 2, 2), nv12.data());
  EXPECT_EQ(nv12[0], 234);
  EXPECT_EQ(nv12[1], 234);
  EXPECT_EQ(nv12[2], 16);
  EXPECT_EQ(nv12[3], 16);
  EXPECT_EQ(nv12[4], 128);
  EXPECT_EQ(nv12[5], 128);
}

}  // namespace
}  // namespace cuttlefish
