#pragma once
/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <pthread.h>
#include <sys/mman.h>
#include <climits>

struct private_handle_t;
struct remoter_request_packet;

namespace vsoc {
namespace framebuffer {
class FBBroadcastRegionView;
}
}

inline size_t roundUpToPageSize(size_t x) {
  return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

class VSoCFrameBuffer {
public:
  static const VSoCFrameBuffer& getInstance();

  static int align(int input, int alignment = kAlignment) {
    return (input + alignment - 1) & -alignment;
  }

  int bits_per_pixel() const {
    return kBitsPerPixel;
  }

  size_t bufferSize() const {
    return line_length_ * y_res();
  }

  int dpi() const;

  int hal_format() const;

  int line_length() const { return line_length_; }

  int total_buffer_size() const {
    return roundUpToPageSize(line_length_ * y_res_virtual() +
                             VSoCFrameBuffer::kSwiftShaderPadding);
  }

  int x_res() const;

  int y_res() const;

  int y_res_virtual() const {
    return y_res() * kNumBuffers;
  }

  static const int kAlignment = 8;
  static const int kNumHwcBuffers = 3;
  // Without sync fences enabled surfaceflinger uses only 2 framebuffers,
  // regardless of how many are available
  static const int kNumSfBuffers = 3;
  static const int kNumBuffers = kNumHwcBuffers + kNumSfBuffers;
  static const char* const kFrameBufferPath;

  static const int kRedShift = 0;
  static const int kRedBits = 8;
  static const int kGreenShift = 8;
  static const int kGreenBits = 8;
  static const int kBlueShift = 16;
  static const int kBlueBits = 8;
  static const int kAlphaShift = 24;
  static const int kAlphaBits = 8;
  typedef uint32_t Pixel;
  static const int kSwiftShaderPadding = 4;

  // Opens the framebuffer file. Ensures the file has the appropriate size by
  // calling ftruncate.
  static bool OpenFrameBuffer(int* frame_buffer_fd);

  // Maps the framebuffer into memory. It's the caller's responsibility to
  // unmap the memory and close the file when done.
  static bool OpenAndMapFrameBuffer(void** fb_memory, int* frame_buffer_fd);
  static bool UnmapAndCloseFrameBuffer(void* fb_memory, int frame_buffer_fd);

private:
  VSoCFrameBuffer();
  void Configure();

  vsoc::framebuffer::FBBroadcastRegionView* fb_region_view_;
  static const int kBitsPerPixel = sizeof(Pixel) * CHAR_BIT;
  // Length of a scan-line in bytes.
  int line_length_;
  DISALLOW_COPY_AND_ASSIGN(VSoCFrameBuffer);
};

const char* pixel_format_to_string(int format);
