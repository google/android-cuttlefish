/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <cstdint>
#include <string>
#include <vector>

#include "cuttlefish/host/graphics_detector/expected.h"

namespace gfxstream {

struct RGBAImage {
  uint32_t width;
  uint32_t height;
  std::vector<uint8_t> pixels;
};
gfxstream::expected<RGBAImage, std::string> LoadRGBAFromBitmapFile(
    const std::string& filename);

gfxstream::expected<Ok, std::string> SaveRGBAToBitmapFile(
    uint32_t w, uint32_t h, const uint8_t* rgbaPixels,
    const std::string& filename = "");

struct YUV420Image {
  uint32_t width;
  uint32_t height;
  std::vector<uint8_t> y;
  std::vector<uint8_t> u;
  std::vector<uint8_t> v;
};
gfxstream::expected<YUV420Image, std::string> LoadYUV420FromBitmapFile(
    const std::string& filename);

RGBAImage FillWithColor(uint32_t width, uint32_t height, uint8_t red,
                        uint8_t green, uint8_t blue, uint8_t alpha);

YUV420Image ConvertRGBA8888ToYUV420(const RGBAImage& image);

struct PixelDiff {
  uint32_t x;
  uint32_t y;
  uint8_t expectedR;
  uint8_t expectedG;
  uint8_t expectedB;
  uint8_t expectedA;
  uint8_t actualR;
  uint8_t actualG;
  uint8_t actualB;
  uint8_t actualA;
};

gfxstream::expected<Ok, std::vector<PixelDiff>> CompareImages(
    const RGBAImage& expected, const RGBAImage& actual);

}  // namespace gfxstream
