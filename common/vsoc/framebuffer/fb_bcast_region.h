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

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/fb_bcast_layout.h"
#include "common/vsoc/shm/graphics.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {
namespace framebuffer {

// TODO(jemoreira): Convert to host only region.
class FBBroadcastRegion : public vsoc::TypedRegionView<
                              vsoc::layout::framebuffer::FBBroadcastLayout> {
 public:
  static FBBroadcastRegion* GetInstance();

  // These values should be initialized by the hwcomposer host daemon, but it
  // will be implemented at a later time, so we return constant values until
  // then.
  struct DisplayProperties {
    // Screen width in pixels
    int x_res() const {
      // TODO(jemoreira): region_->data()->x_res;
      return kFbXres;
    }
    // Screen height in pixels
    int y_res() const {
      // TODO(jemoreira): region_->data()->y_res;
      return kFbYres;
    }
    // Dots per inch
    int dpi() const {
      // TODO(jemoreira): region_->data()->dpi;
      return kFbDpi;
    }
    // Refresh rate in Hertz
    int refresh_rate_hz() const {
      // TODO(jemoreira): region_->data()->refresh_rate_hz;
      return kFbRefreshRateHz;
    }
    constexpr uint32_t pixel_format() const {
      return kFbPixelFormat;
    }
    constexpr uint32_t bytes_per_pixel() const {
      return vsoc::PixelFormatProperties<kFbPixelFormat>::bytes_per_pixel;
    }
    DisplayProperties(FBBroadcastRegion* /*region*/) /*: region_(region)*/ {}
   private:
    static constexpr uint32_t kFbPixelFormat = vsoc::VSOC_PIXEL_FORMAT_RGBA_8888;
    static constexpr int kFbXres = 800;
    static constexpr int kFbYres = 1280;
    static constexpr int kFbDpi = 160;
    static constexpr int kFbRefreshRateHz = 60;
    // FBBroadcastRegion* region_;
  };

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

  const DisplayProperties& display_properties() const {
    return properties_;
  }

 protected:
  FBBroadcastRegion();
  FBBroadcastRegion(const FBBroadcastRegion&);
  FBBroadcastRegion& operator=(const FBBroadcastRegion&);

  bool is_open_;
  const DisplayProperties properties_;
};

} // namespace framebuffer
} // namespace vsoc
