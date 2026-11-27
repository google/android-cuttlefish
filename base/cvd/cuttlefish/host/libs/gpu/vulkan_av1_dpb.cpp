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

#include "cuttlefish/host/libs/gpu/vulkan_av1_dpb.h"

#include <stdint.h>

#include <algorithm>
#include <optional>

#include "vk_video/vulkan_video_codec_av1std.h"

namespace cuttlefish {

Av1DpbPingPong::Av1DpbPingPong(uint32_t slot_count)
    : slot_count_(std::max(1u, slot_count)) {}

int32_t Av1DpbPingPong::SetupSlot(bool key_frame) const {
  if (key_frame || !reference_slot_.has_value()) {
    return 0;
  }
  return (*reference_slot_ + 1) % static_cast<int32_t>(slot_count_);
}

void Av1DpbPingPong::Commit(bool key_frame, uint8_t order_hint) {
  const int32_t slot = SetupSlot(key_frame);
  if (key_frame) {
    ref_order_hints_.fill(order_hint);
  } else {
    ref_order_hints_[slot] = order_hint;
  }
  reference_slot_ = slot;
  reference_order_hint_ = order_hint;
  reference_frame_type_ =
      key_frame ? STD_VIDEO_AV1_FRAME_TYPE_KEY : STD_VIDEO_AV1_FRAME_TYPE_INTER;
}

}  // namespace cuttlefish
