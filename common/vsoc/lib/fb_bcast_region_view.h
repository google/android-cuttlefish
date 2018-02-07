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
#include "common/vsoc/shm/fb_bcast_layout.h"
#include "common/vsoc/shm/graphics.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {
namespace framebuffer {

// Provides information related to the device's screen. Allows to query screen
// properties such as resolution and dpi, as well as subscribe/notify to/of
// changes on the screen contents. It's independent of where the buffer holding
// the screen contents is. This region will eventually become the display
// region, which will represent display hardware including the hardware
// composer.
class FBBroadcastRegionView
    : public vsoc::TypedRegionView<
          vsoc::layout::framebuffer::FBBroadcastLayout> {
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

  // The framebuffer broadcast region mantains a bit set to keep track of the
  // buffers that have been allocated already. This function atomically finds an
  // unset (0) bit in the set, sets it to 1 and returns it. The filter parameter
  // specifies which bits to consider from the set.
  // TODO(jemoreira): Move frame buffers to the gralloc region and remove this
  // bitset.
  uint32_t GetAndSetNextAvailableBufferBit(uint32_t filter);
  void UnsetBufferBits(uint32_t bits);

  // Broadcasts a new frame.
  // frame_offset is the offset of the current frame in the framebuffer region.
  // stats holds performance information of the last composition, can be null.
  void BroadcastNewFrame(
      vsoc_reg_off_t frame_offset,
      const vsoc::layout::framebuffer::CompositionStats* stats = nullptr);

  // Waits for a new frame (one with a different seq_num than last one we saw).
  // Returns the offset of the new frame or zero if there was an error, stores
  // the new sequential number in *last_seq_num. The frame sequential number are
  // provided by the hwcomposer and expected to increase monotonically over time
  // (though it's not a hard requirement), this numbers are guaranteed to be
  // non-zero when a valid frame is available. Performance statistics are
  // returned through the stats parameter when it's not null.
  vsoc_reg_off_t WaitForNewFrameSince(
      uint32_t* last_seq_num,
      vsoc::layout::framebuffer::CompositionStats* stats = nullptr);

#if defined(CUTTLEFISH_HOST)
  static std::shared_ptr<FBBroadcastRegionView> GetInstance(const char* domain);
#else
  static std::shared_ptr<FBBroadcastRegionView> GetInstance();
#endif

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
};
}  // namespace framebuffer
}  // namespace vsoc
