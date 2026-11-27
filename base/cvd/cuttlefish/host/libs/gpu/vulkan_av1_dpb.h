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

#pragma once

#include <stdint.h>

#include <array>
#include <optional>

#include "vk_video/vulkan_video_codec_av1std.h"

namespace cuttlefish {

// Two slot ping-pong bookkeeping for low delay P frames: an inter frame
// references the slot holding the previous reconstructed picture and writes
// its own into the other slot. A key frame drops the reference and starts
// again from slot 0. AV1's eight reference buffers are addressed by the same
// index as the DPB slot, so a frame refreshing slot n also refreshes buffer
// n.
class Av1DpbPingPong {
 public:
  explicit Av1DpbPingPong(uint32_t slot_count);

  // The slot the next frame reconstructs into.
  int32_t SetupSlot(bool key_frame) const;
  // The slot an inter frame references, if there is one yet.
  std::optional<int32_t> ReferenceSlot() const { return reference_slot_; }
  uint8_t ReferenceOrderHint() const { return reference_order_hint_; }
  StdVideoAV1FrameType ReferenceFrameType() const {
    return reference_frame_type_;
  }
  // The order hints a decoder holds for AV1's eight reference buffers. Every
  // inter frame carries them while error resilient mode is on.
  const std::array<uint8_t, STD_VIDEO_AV1_NUM_REF_FRAMES>& RefOrderHints()
      const {
    return ref_order_hints_;
  }

  // Records a frame the device accepted.
  void Commit(bool key_frame, uint8_t order_hint);

 private:
  uint32_t slot_count_;
  std::optional<int32_t> reference_slot_;
  uint8_t reference_order_hint_ = 0;
  StdVideoAV1FrameType reference_frame_type_ = STD_VIDEO_AV1_FRAME_TYPE_KEY;
  std::array<uint8_t, STD_VIDEO_AV1_NUM_REF_FRAMES> ref_order_hints_ = {};
};

}  // namespace cuttlefish
