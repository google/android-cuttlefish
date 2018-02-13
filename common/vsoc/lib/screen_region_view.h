#pragma once
/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <memory>

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/graphics.h"
#include "common/vsoc/shm/screen_layout.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {
namespace screen {

// Provides information related to the device's screen. Allows to query screen
// properties such as resolution and dpi, as well as subscribe/notify to/of
// changes on the screen contents. It also holds the contents of the display.
class ScreenRegionView
    : public vsoc::TypedRegionView<ScreenRegionView,
                                   vsoc::layout::screen::ScreenLayout> {
 public:
  static int align(int input, int alignment = kAlignment) {
    return (input + alignment - 1) & -alignment;
  }

  // Screen width in pixels
  int x_res() const { return data().x_res; }

  // Screen height in pixels
  int y_res() const { return data().y_res; }

  // Dots per inch
  int dpi() const { return data().dpi; }

  // Refresh rate in Hertz
  int refresh_rate_hz() const { return data().refresh_rate_hz; }

  uint32_t pixel_format() const { return kFbPixelFormat; }

  uint32_t bytes_per_pixel() const {
    return vsoc::PixelFormatProperties<kFbPixelFormat>::bytes_per_pixel;
  }

  int line_length() const { return align(x_res() * bytes_per_pixel()); }

  size_t buffer_size() const {
    return (align(x_res() * bytes_per_pixel()) * y_res()) + kSwiftShaderPadding;
  }

  int number_of_buffers() const;

  // Gets a pointer to the beginning of a buffer. Does not perform any bound
  // checks on the index.
  void* GetBuffer(int buffer_idx);

  // Broadcasts a new frame.
  // buffer_idx is the index of the buffer containing the composed screen, it's
  // a number in the range [0, number_of_buffers() - 1].
  // Stats holds performance information of the last composition, can be null.
  void BroadcastNewFrame(
      int buffer_idx,
      const vsoc::layout::screen::CompositionStats* stats = nullptr);

  // Waits for a new frame (one with a different seq_num than last one we saw).
  // Returns the index of the buffer containing the new frame or a negative
  // number if there was an error, stores the new sequential number in
  // *last_seq_num. The frame sequential numbers will be provided by the
  // hwcomposer and expected to increase monotonically over time (though it's
  // not a hard requirement), this numbers are guaranteed to be non-zero when a
  // valid frame is available. Performance statistics are returned through the
  // stats parameter when it's not null.
  int WaitForNewFrameSince(
      uint32_t* last_seq_num,
      vsoc::layout::screen::CompositionStats* stats = nullptr);

  using Pixel = uint32_t;
  static constexpr int kSwiftShaderPadding = 4;
  static constexpr int kRedShift = 0;
  static constexpr int kGreenShift = 8;
  static constexpr int kBlueShift = 16;
  static constexpr int kRedBits = 8;
  static constexpr int kGreenBits = 8;
  static constexpr int kBlueBits = 8;
  static constexpr uint32_t kFbPixelFormat = vsoc::VSOC_PIXEL_FORMAT_RGBA_8888;
  static constexpr int kAlignment = 8;

 protected:
  const uint8_t* first_buffer() const;
};
}  // namespace screen
}  // namespace vsoc
