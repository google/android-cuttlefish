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

#include "host/libs/graphics_detector/img.h"

#include <fstream>
#include <ostream>

#include <android-base/logging.h>

namespace cuttlefish {

// Loads:
//   rgba_pixels[0] = R for x:0 y:0
//   rgba_pixels[1] = G for x:0 y:0
//   rgba_pixels[2] = B for x:0 y:0
//   rgba_pixels[3] = A for x:0 y:0
void LoadRGBAFromBitmapFile(const std::string& filename, uint32_t* out_w,
                            uint32_t* out_h, std::vector<uint8_t>* out_pixels) {
  *out_w = 0;
  *out_h = 0;
  out_pixels->clear();

  std::ifstream bitmap(filename, std::ofstream::in | std::ofstream::binary);
  if (!bitmap.is_open()) {
    LOG(ERROR) << "Failed to open " << filename;
    return;
  }

  std::vector<char> bitmap_bytes((std::istreambuf_iterator<char>(bitmap)),
                                 std::istreambuf_iterator<char>());

  if (bitmap_bytes[0] != 0x42) {
    LOG(ERROR) << "Invalid bitmap file?";
    return;
  }
  if (bitmap_bytes[1] != 0x4D) {
    LOG(ERROR) << "Invalid bitmap file?";
    return;
  }

  auto ReadUint16AtByte = [&](const uint32_t offset) {
    return *reinterpret_cast<uint16_t*>(&bitmap_bytes[offset]);
  };
  auto ReadUint32AtByte = [&](const uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(&bitmap_bytes[offset]);
  };

  uint32_t w = ReadUint32AtByte(18);
  uint32_t h = ReadUint32AtByte(22);
  LOG(ERROR) << "Loading " << filename << " w:" << w << " h:" << h;

  uint32_t planes = ReadUint16AtByte(26);
  if (planes != 1) {
    LOG(ERROR) << "Unhandled number of planes: " << planes;
    return;
  }
  uint32_t bits_per_pixel = ReadUint16AtByte(28);
  if (bits_per_pixel != 32) {
    LOG(ERROR) << "Unhandled number of bpp: " << bits_per_pixel;
    return;
  }

  uint32_t r_channel_mask = ReadUint32AtByte(54);
  uint32_t g_channel_mask = ReadUint32AtByte(58);
  uint32_t b_channel_mask = ReadUint32AtByte(62);
  uint32_t a_channel_mask = ReadUint32AtByte(66);

  /*
  LOG(ERROR) << " r_channel_mask:" << r_channel_mask
             << " g_channel_mask:" << g_channel_mask
             << " b_channel_mask:" << b_channel_mask
             << " a_channel_mask:" << a_channel_mask;
  */

  *out_w = w;
  *out_h = h;
  out_pixels->clear();
  out_pixels->reserve(w * h * 4);

  uint32_t bitmap_headers_size = ReadUint32AtByte(10);
  uint32_t bitmap_pixels_offset = bitmap_headers_size;

  auto GetChannel = [](uint32_t pixel, uint32_t channel_mask) {
    if (channel_mask == 0) {
      return static_cast<uint8_t>(0xFF);
    } else if (channel_mask == 0x000000FF) {
      return static_cast<uint8_t>((pixel & channel_mask) >> 0);
    } else if (channel_mask == 0x0000FF00) {
      return static_cast<uint8_t>((pixel & channel_mask) >> 8);
    } else if (channel_mask == 0x00FF0000) {
      return static_cast<uint8_t>((pixel & channel_mask) >> 16);
    } else if (channel_mask == 0xFF000000) {
      return static_cast<uint8_t>((pixel & channel_mask) >> 24);
    } else {
      LOG(FATAL) << "Unhandled channel mask: " << channel_mask;
      return static_cast<uint8_t>(0);
    }
  };

  for (uint32_t y = 0; y < h; y++) {
    uint32_t flipped_y = h - y - 1;
    for (uint32_t x = 0; x < w; x++) {
      uint32_t pixel_offset = (flipped_y * w * 4) + (x * 4);
      uint32_t pixel = ReadUint32AtByte(bitmap_pixels_offset + pixel_offset);

      uint8_t r = GetChannel(pixel, r_channel_mask);
      uint8_t g = GetChannel(pixel, g_channel_mask);
      uint8_t b = GetChannel(pixel, b_channel_mask);
      uint8_t a = GetChannel(pixel, a_channel_mask);

      out_pixels->push_back(r);
      out_pixels->push_back(g);
      out_pixels->push_back(b);
      out_pixels->push_back(a);

#if 0
      LOG(ERROR) << " r_channel_mask:" << r_channel_mask
                 << " g_channel_mask:" << g_channel_mask
                 << " b_channel_mask:" << b_channel_mask
                 << " a_channel_mask:" << a_channel_mask
                 << " pixel:" << pixel;
#endif
#if 0
      LOG(ERROR) << " x:" << x
                 << " y:" << y
                 << " r:" << (int)r
                 << " g:" << (int)g
                 << " b:" << (int)b
                 << " a:" << (int)a;
#endif
    }
  }
}

// Assumes:
//   rgba_pixels[0] = R for x:0 y:0
//   rgba_pixels[1] = G for x:0 y:0
//   rgba_pixels[2] = B for x:0 y:0
//   rgba_pixels[3] = A for x:0 y:0
void SaveRGBAToBitmapFile(uint32_t w, uint32_t h, const uint8_t* rgba_pixels,
                          const std::string& filename) {
  std::ofstream bitmap(filename, std::ofstream::out | std::ofstream::binary);
  if (!bitmap.is_open()) {
    LOG(ERROR) << "Failed to open " << filename;
    return;
  }

  static constexpr const uint32_t kBytesPerPixel = 4;
  uint32_t bitmap_pixels_size = w * h * kBytesPerPixel;
  uint32_t bitmap_header_size = 14;
  uint32_t bitmap_dib_header_size = 108;
  uint32_t bitmap_headers_size = bitmap_header_size + bitmap_dib_header_size;
  uint32_t bitmap_file_size = bitmap_headers_size + bitmap_pixels_size;

  auto WriteAsBytes = [&](const auto& value) {
    bitmap.write(reinterpret_cast<const char*>(&value), sizeof(value));
  };
  auto WriteCharAsBytes = [&](const char value) { WriteAsBytes(value); };
  auto WriteUint16AsBytes = [&](const uint16_t value) { WriteAsBytes(value); };
  auto WriteUint32AsBytes = [&](const uint32_t value) { WriteAsBytes(value); };

  WriteCharAsBytes(0x42);  // "B"
  WriteCharAsBytes(0x4D);  // "M"
  WriteUint32AsBytes(bitmap_file_size);
  WriteCharAsBytes(0);                      // reserved 1
  WriteCharAsBytes(0);                      // reserved 1
  WriteCharAsBytes(0);                      // reserved 2
  WriteCharAsBytes(0);                      // reserved 2
  WriteUint32AsBytes(bitmap_headers_size);  // offset to actual pixel data
  WriteUint32AsBytes(bitmap_dib_header_size);
  WriteUint32AsBytes(w);
  WriteUint32AsBytes(h);
  WriteUint16AsBytes(1);                   // number of planes
  WriteUint16AsBytes(32);                  // bits per pixel
  WriteUint32AsBytes(0x03);                // compression/format
  WriteUint32AsBytes(bitmap_pixels_size);  // image size
  WriteUint32AsBytes(0);                   // horizontal print reset
  WriteUint32AsBytes(0);                   // vertical print reset
  WriteUint32AsBytes(0);                   // num_palette_colors
  WriteUint32AsBytes(0);                   // num_important_colors
  WriteUint32AsBytes(0x000000FF);          // red channel mask
  WriteUint32AsBytes(0x0000FF00);          // green channel mask
  WriteUint32AsBytes(0x00FF0000);          // blue channel mask
  WriteUint32AsBytes(0xFF000000);          // alpha channel mask
  WriteUint32AsBytes(0x206e6957);          // "win"
  for (uint32_t i = 0; i < 36; i++) {
    WriteCharAsBytes(0);
  }                       // cie color space
  WriteUint32AsBytes(0);  // "win"
  WriteUint32AsBytes(0);  // "win"
  WriteUint32AsBytes(0);  // "win"

  uint32_t stride_bytes = w * 4;
  for (uint32_t current_y = 0; current_y < h; current_y++) {
    uint32_t flipped_y = h - current_y - 1;

    const uint8_t* current_pixel = rgba_pixels + (stride_bytes * flipped_y);
    for (uint32_t current_x = 0; current_x < w; current_x++) {
      WriteAsBytes(*current_pixel);
      ++current_pixel;
      WriteAsBytes(*current_pixel);
      ++current_pixel;
      WriteAsBytes(*current_pixel);
      ++current_pixel;
      WriteAsBytes(*current_pixel);
      ++current_pixel;
    }
  }

  bitmap.close();
  LOG(INFO) << "Saved bitmap to " << filename;
}

void LoadYUV420FromBitmapFile(const std::string& filename, uint32_t* out_w,
                              uint32_t* out_h, std::vector<uint8_t>* out_y,
                              std::vector<uint8_t>* out_u,
                              std::vector<uint8_t>* out_v) {
  std::vector<uint8_t> rgba;

  LoadRGBAFromBitmapFile(filename, out_w, out_h, &rgba);

  if (rgba.empty()) return;

  ConvertRGBA8888ToYUV420(*out_w, *out_h, rgba, out_y, out_u, out_v);
}

void FillWithColor(uint32_t width, uint32_t height, uint8_t red, uint8_t green,
                   uint8_t blue, uint8_t alpha,
                   std::vector<uint8_t>* out_pixels) {
  out_pixels->clear();
  out_pixels->reserve(width * height * 4);
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      out_pixels->push_back(red);
      out_pixels->push_back(green);
      out_pixels->push_back(blue);
      out_pixels->push_back(alpha);
    }
  }
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

void ConvertRGBA8888ToYUV420(uint32_t w, uint32_t h,
                             const std::vector<uint8_t>& rgba_pixels,
                             std::vector<uint8_t>* y_pixels,
                             std::vector<uint8_t>* u_pixels,
                             std::vector<uint8_t>* v_pixels) {
  y_pixels->reserve(w * h);
  u_pixels->reserve((w / 2) * (h / 2));
  v_pixels->reserve((w / 2) * (h / 2));

  const auto* input = rgba_pixels.data();
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

      uint8_t pixel_y;
      uint8_t pixel_u;
      uint8_t pixel_v;
      ConvertRGBA8888PixelToYUV(r, g, b, &pixel_y, &pixel_u, &pixel_v);

      y_pixels->push_back(pixel_y);
      if ((x % 2 == 0) && (y % 2 == 0)) {
        u_pixels->push_back(pixel_u);
        v_pixels->push_back(pixel_v);
      }
    }
  }
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

bool ImagesAreSimilar(uint32_t width, uint32_t height,
                      const std::vector<uint8_t>& image1_rgba8888,
                      const std::vector<uint8_t>& image2_rgba8888) {
  bool images_are_similar = true;

  uint32_t reported_incorrect_pixels = 0;
  constexpr const uint32_t kMaxReportedIncorrectPixels = 10;

  const uint32_t* image1_pixels =
      reinterpret_cast<const uint32_t*>(image1_rgba8888.data());
  const uint32_t* image2_pixels =
      reinterpret_cast<const uint32_t*>(image2_rgba8888.data());

  for (uint32_t y = 0; y < width; y++) {
    for (uint32_t x = 0; x < height; x++) {
      const uint32_t image1_pixel = image1_pixels[y * height + x];
      const uint32_t image2_pixel = image2_pixels[y * height + x];
      if (!PixelsAreSimilar(image1_pixel, image2_pixel)) {
        images_are_similar = false;
        if (reported_incorrect_pixels < kMaxReportedIncorrectPixels) {
          reported_incorrect_pixels++;
          const uint8_t* image1_pixel_rgba =
              reinterpret_cast<const uint8_t*>(&image1_pixel);
          const uint8_t* image2_pixel_rgba =
              reinterpret_cast<const uint8_t*>(&image2_pixel);
          LOG(ERROR) << "Pixel comparison failed at (" << x << ", " << y << ") "
                     << " with "
                     << " r:" << static_cast<int>(image1_pixel_rgba[0])
                     << " g:" << static_cast<int>(image1_pixel_rgba[1])
                     << " b:" << static_cast<int>(image1_pixel_rgba[2])
                     << " a:" << static_cast<int>(image1_pixel_rgba[3])
                     << " versus "
                     << " r:" << static_cast<int>(image2_pixel_rgba[0])
                     << " g:" << static_cast<int>(image2_pixel_rgba[1])
                     << " b:" << static_cast<int>(image2_pixel_rgba[2])
                     << " a:" << static_cast<int>(image2_pixel_rgba[3]);
        }
      }
    }
  }

  return images_are_similar;
}

}  // namespace cuttlefish