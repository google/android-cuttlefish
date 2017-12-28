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

class FBBroadcastRegionView
    : public vsoc::TypedRegionView<
          vsoc::layout::framebuffer::FBBroadcastLayout> {
 public:
  // Screen width in pixels
  int x_res() const { return data().x_res; }

  // Screen height in pixels
  int y_res() const { return data().y_res; }

  // Dots per inch
  int dpi() const { return data().dpi; }

  // Refresh rate in Hertz
  int refresh_rate_hz() const {
    // TODO(jemoreira): region_->data()->refresh_rate_hz;
    return kFbRefreshRateHz;
  }

  uint32_t pixel_format() const { return kFbPixelFormat; }

  uint32_t bytes_per_pixel() const {
    return vsoc::PixelFormatProperties<kFbPixelFormat>::bytes_per_pixel;
  }

  // Broadcasts a new frame. Meant to be used exclusively by the hwcomposer.
  // Zero is an invalid frame_num, used to indicate that there is no frame
  // available. It's expected that frame_num increases monotonically over time,
  // but there is no hard requirement for this.
  // frame_offset is the offset of the current frame in the gralloc region.
  void BroadcastNewFrame(uint32_t frame_num, vsoc_reg_off_t frame_offset);

  // Waits for a new frame (one with a different seq_num than last one we saw).
  // Returns the offset of the new frame or zero if there was an error, stores
  // the new sequential number in *last_seq_num.
  vsoc_reg_off_t WaitForNewFrameSince(uint32_t* last_seq_num);

#if defined(CUTTLEFISH_HOST)
  static std::shared_ptr<FBBroadcastRegionView> GetInstance(const char* domain);
#else
  static std::shared_ptr<FBBroadcastRegionView> GetInstance();
#endif

 private:
  static constexpr uint32_t kFbPixelFormat = vsoc::VSOC_PIXEL_FORMAT_RGBA_8888;
  static constexpr int kFbRefreshRateHz = 60;
};
}  // namespace framebuffer
}  // namespace vsoc
