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

#include "cuttlefish/host/graphics_detector/image.h"

#include <fstream>
#include <ostream>

namespace gfxstream {

// Loads:
//   rgbaPixels[0] = R for x:0 y:0
//   rgbaPixels[1] = G for x:0 y:0
//   rgbaPixels[2] = B for x:0 y:0
//   rgbaPixels[3] = A for x:0 y:0
gfxstream::expected<RGBAImage, std::string> LoadRGBAFromBitmapFile(
    const std::string& filename) {
  std::ifstream bitmap(filename, std::ofstream::in | std::ofstream::binary);
  if (!bitmap.is_open()) {
    return gfxstream::unexpected("Failed to open " + filename);
  }

  std::vector<char> bitmapBytes((std::istreambuf_iterator<char>(bitmap)),
                                std::istreambuf_iterator<char>());

  if (bitmapBytes[0] != 0x42) {
    return gfxstream::unexpected("Failed to open " + filename +
                                 ": invalid bitmap file?");
  }
  if (bitmapBytes[1] != 0x4D) {
    return gfxstream::unexpected("Failed to open " + filename +
                                 ": invalid bitmap file?");
  }

  auto ReadUint16AtByte = [&](const uint32_t offset) {
    return *reinterpret_cast<uint16_t*>(&bitmapBytes[offset]);
  };
  auto ReadUint32AtByte = [&](const uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(&bitmapBytes[offset]);
  };

  uint32_t w = ReadUint32AtByte(18);
  uint32_t h = ReadUint32AtByte(22);

  uint32_t planes = ReadUint16AtByte(26);
  if (planes != 1) {
    return gfxstream::unexpected("Failed to open " + filename +
                                 ": unhandled number of planes.");
  }
  uint32_t bpp = ReadUint16AtByte(28);
  if (bpp != 32) {
    return gfxstream::unexpected("Failed to open " + filename +
                                 ": unhandled bpp.");
  }

  uint32_t rChannelMask = ReadUint32AtByte(54);
  uint32_t gChannelMask = ReadUint32AtByte(58);
  uint32_t bChannelMask = ReadUint32AtByte(62);
  uint32_t aChannelMask = ReadUint32AtByte(66);

  RGBAImage out;
  out.width = w;
  out.height = h;
  out.pixels.reserve(w * h * 4);

  uint32_t bitmapHeadersSize = ReadUint32AtByte(10);
  uint32_t bitmapPixelsOffset = bitmapHeadersSize;

  auto GetChannel = [](uint32_t pixel, uint32_t channelMask) {
    if (channelMask == 0) {
      return static_cast<uint8_t>(0xFF);
    } else if (channelMask == 0x000000FF) {
      return static_cast<uint8_t>((pixel & channelMask) >> 0);
    } else if (channelMask == 0x0000FF00) {
      return static_cast<uint8_t>((pixel & channelMask) >> 8);
    } else if (channelMask == 0x00FF0000) {
      return static_cast<uint8_t>((pixel & channelMask) >> 16);
    } else if (channelMask == 0xFF000000) {
      return static_cast<uint8_t>((pixel & channelMask) >> 24);
    } else {
      return static_cast<uint8_t>(0);
    }
  };

  for (uint32_t y = 0; y < h; y++) {
    uint32_t flippedY = h - y - 1;
    for (uint32_t x = 0; x < w; x++) {
      uint32_t pixelOffset = (flippedY * w * 4) + (x * 4);
      uint32_t pixel = ReadUint32AtByte(bitmapPixelsOffset + pixelOffset);

      uint8_t r = GetChannel(pixel, rChannelMask);
      uint8_t g = GetChannel(pixel, gChannelMask);
      uint8_t b = GetChannel(pixel, bChannelMask);
      uint8_t a = GetChannel(pixel, aChannelMask);

      out.pixels.push_back(r);
      out.pixels.push_back(g);
      out.pixels.push_back(b);
      out.pixels.push_back(a);
    }
  }

  return out;
}

// Assumes:
//   rgbaPixels[0] = R for x:0 y:0
//   rgbaPixels[1] = G for x:0 y:0
//   rgbaPixels[2] = B for x:0 y:0
//   rgbaPixels[3] = A for x:0 y:0
gfxstream::expected<Ok, std::string> SaveRGBAToBitmapFile(
    uint32_t w, uint32_t h, const uint8_t* rgbaPixels,
    const std::string& filename) {
  std::ofstream bitmap(filename, std::ofstream::out | std::ofstream::binary);
  if (!bitmap.is_open()) {
    return gfxstream::unexpected("Failed to save " + filename +
                                 ": failed to open.");
  }

  static constexpr const uint32_t kBytesPerPixel = 4;
  uint32_t bitmapPixelsSize = w * h * kBytesPerPixel;
  uint32_t bitmapHeaderSize = 14;
  uint32_t bitmapDibHeaderSize = 108;
  uint32_t bitmapHeadersSize = bitmapHeaderSize + bitmapDibHeaderSize;
  uint32_t bitmapFileSize = bitmapHeadersSize + bitmapPixelsSize;

  auto WriteAsBytes = [&](const auto& value) {
    bitmap.write(reinterpret_cast<const char*>(&value), sizeof(value));
  };
  auto WriteCharAsBytes = [&](const char value) { WriteAsBytes(value); };
  auto WriteUint16AsBytes = [&](const uint16_t value) { WriteAsBytes(value); };
  auto WriteUint32AsBytes = [&](const uint32_t value) { WriteAsBytes(value); };

  WriteCharAsBytes(0x42);  // "B"
  WriteCharAsBytes(0x4D);  // "M"
  WriteUint32AsBytes(bitmapFileSize);
  WriteCharAsBytes(0);                    // reserved 1
  WriteCharAsBytes(0);                    // reserved 1
  WriteCharAsBytes(0);                    // reserved 2
  WriteCharAsBytes(0);                    // reserved 2
  WriteUint32AsBytes(bitmapHeadersSize);  // offset to actual pixel data
  WriteUint32AsBytes(bitmapDibHeaderSize);
  WriteUint32AsBytes(w);
  WriteUint32AsBytes(h);
  WriteUint16AsBytes(1);                 // number of planes
  WriteUint16AsBytes(32);                // bits per pixel
  WriteUint32AsBytes(0x03);              // compression/format
  WriteUint32AsBytes(bitmapPixelsSize);  // image size
  WriteUint32AsBytes(0);                 // horizontal print reset
  WriteUint32AsBytes(0);                 // vertical print reset
  WriteUint32AsBytes(0);                 // num_palette_colors
  WriteUint32AsBytes(0);                 // num_important_colors
  WriteUint32AsBytes(0x000000FF);        // red channel mask
  WriteUint32AsBytes(0x0000FF00);        // green channel mask
  WriteUint32AsBytes(0x00FF0000);        // blue channel mask
  WriteUint32AsBytes(0xFF000000);        // alpha channel mask
  WriteUint32AsBytes(0x206e6957);        // "win"
  for (uint32_t i = 0; i < 36; i++) {
    WriteCharAsBytes(0);
  }  // cie color space
  WriteUint32AsBytes(0);  // "win"
  WriteUint32AsBytes(0);  // "win"
  WriteUint32AsBytes(0);  // "win"

  uint32_t stribeBytes = w * 4;
  for (uint32_t currentY = 0; currentY < h; currentY++) {
    uint32_t flippedY = h - currentY - 1;
    const uint8_t* currentPixel = rgbaPixels + (stribeBytes * flippedY);
    for (uint32_t currentX = 0; currentX < w; currentX++) {
      WriteAsBytes(*currentPixel);
      ++currentPixel;
      WriteAsBytes(*currentPixel);
      ++currentPixel;
      WriteAsBytes(*currentPixel);
      ++currentPixel;
      WriteAsBytes(*currentPixel);
      ++currentPixel;
    }
  }

  bitmap.close();

  return Ok{};
}

gfxstream::expected<YUV420Image, std::string> LoadYUV420FromBitmapFile(
    const std::string& filename) {
  auto rgbaImage = GFXSTREAM_EXPECT(LoadRGBAFromBitmapFile(filename));
  return ConvertRGBA8888ToYUV420(rgbaImage);
}

RGBAImage FillWithColor(uint32_t width, uint32_t height, uint8_t red,
                        uint8_t green, uint8_t blue, uint8_t alpha) {
  RGBAImage out;
  out.width = width;
  out.height = height;
  out.pixels.reserve(width * height * 4);
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      out.pixels.push_back(red);
      out.pixels.push_back(green);
      out.pixels.push_back(blue);
      out.pixels.push_back(alpha);
    }
  }
  return out;
}

namespace {

uint8_t Clamp(int x) {
  if (x > 255) {
    return 255;
  }
  if (x < 0) {
    return 0;
  }
  return static_cast<uint8_t>(x);
}

// BT.601 with "Studio Swing" / narrow range.
void ConvertRGBA8888PixelToYUV(const uint8_t r, const uint8_t g,
                               const uint8_t b, uint8_t* out_y, uint8_t* out_u,
                               uint8_t* out_v) {
  const int r_int = static_cast<int>(r);
  const int g_int = static_cast<int>(g);
  const int b_int = static_cast<int>(b);
  *out_y =
      Clamp((((66 * r_int) + (129 * g_int) + (25 * b_int) + 128) >> 8) + 16);
  *out_u =
      Clamp((((-38 * r_int) - (74 * g_int) + (112 * b_int) + 128) >> 8) + 128);
  *out_v =
      Clamp((((112 * r_int) - (94 * g_int) - (18 * b_int) + 128) >> 8) + 128);
}

}  // namespace

YUV420Image ConvertRGBA8888ToYUV420(const RGBAImage& rgbaImage) {
  const uint32_t w = rgbaImage.width;
  const uint32_t h = rgbaImage.height;

  YUV420Image yuvImage;
  yuvImage.width = w;
  yuvImage.height = h;
  yuvImage.y.reserve(w * h);
  yuvImage.u.reserve((w / 2) * (h / 2));
  yuvImage.v.reserve((w / 2) * (h / 2));

  const auto* input = rgbaImage.pixels.data();
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      const uint8_t r = *input;
      ++input;
      const uint8_t g = *input;
      ++input;
      const uint8_t b = *input;
      ++input;
      // const uint8_t a = *input;
      ++input;

      uint8_t pixelY;
      uint8_t pixelU;
      uint8_t pixelV;
      ConvertRGBA8888PixelToYUV(r, g, b, &pixelY, &pixelU, &pixelV);

      yuvImage.y.push_back(pixelY);
      if ((x % 2 == 0) && (y % 2 == 0)) {
        yuvImage.u.push_back(pixelU);
        yuvImage.v.push_back(pixelV);
      }
    }
  }

  return yuvImage;
}

namespace {

bool PixelsAreSimilar(uint32_t pixel1, uint32_t pixel2) {
  const uint8_t* pixel1_rgba = reinterpret_cast<const uint8_t*>(&pixel1);
  const uint8_t* pixel2_rgba = reinterpret_cast<const uint8_t*>(&pixel2);

  constexpr const uint32_t kDefaultTolerance = 2;
  for (uint32_t channel = 0; channel < 4; channel++) {
    const uint8_t pixel1_channel = pixel1_rgba[channel];
    const uint8_t pixel2_channel = pixel2_rgba[channel];
    if ((std::max(pixel1_channel, pixel2_channel) -
         std::min(pixel1_channel, pixel2_channel)) > kDefaultTolerance) {
      return false;
    }
  }
  return true;
}

}  // namespace

gfxstream::expected<Ok, std::vector<PixelDiff>> CompareImages(
    const RGBAImage& expected, const RGBAImage& actual) {
  const uint32_t width = expected.width;
  const uint32_t height = expected.height;

  constexpr const uint32_t kMaxReportedIncorrectPixels = 10;

  const auto* expectedPixels =
      reinterpret_cast<const uint32_t*>(expected.pixels.data());
  const auto* actualPixels =
      reinterpret_cast<const uint32_t*>(actual.pixels.data());

  std::vector<PixelDiff> pixelDiffs;

  for (uint32_t y = 0; y < width; y++) {
    for (uint32_t x = 0; x < height; x++) {
      const uint32_t expectedPixel = expectedPixels[y * height + x];
      const uint32_t actualPixel = actualPixels[y * height + x];
      if (!PixelsAreSimilar(expectedPixel, actualPixel)) {
        if (pixelDiffs.size() < kMaxReportedIncorrectPixels) {
          const uint8_t* expectedPixelRgba =
              reinterpret_cast<const uint8_t*>(&expectedPixel);
          const uint8_t* actualPixelRgba =
              reinterpret_cast<const uint8_t*>(&actualPixel);
          pixelDiffs.push_back(PixelDiff{
              .x = x,
              .y = y,
              .expectedR = expectedPixelRgba[0],
              .expectedG = expectedPixelRgba[1],
              .expectedB = expectedPixelRgba[2],
              .expectedA = expectedPixelRgba[3],
              .actualR = actualPixelRgba[0],
              .actualG = actualPixelRgba[1],
              .actualB = actualPixelRgba[2],
              .actualA = actualPixelRgba[3],
          });
        }
      }
    }
  }

  if (!pixelDiffs.empty()) {
    return gfxstream::unexpected(std::move(pixelDiffs));
  }

  return Ok{};
}

}  // namespace gfxstream