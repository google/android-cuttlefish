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

#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_encode.h"
#include "vulkan/vulkan_core.h"

namespace cuttlefish {

// Width of the frame order hint the sequence header declares.
constexpr uint32_t kAv1OrderHintBits = 8;

// The AV1 sequence header and operating point the session parameters are
// created from, for a coded size. The structures point at each other, so
// this is neither copied nor moved.
class Av1SequenceHeaderInfo {
 public:
  explicit Av1SequenceHeaderInfo(VkExtent2D coded_extent);

  Av1SequenceHeaderInfo(const Av1SequenceHeaderInfo&) = delete;
  Av1SequenceHeaderInfo& operator=(const Av1SequenceHeaderInfo&) = delete;

  const StdVideoAV1SequenceHeader& sequence_header() const {
    return sequence_header_;
  }
  const StdVideoEncodeAV1OperatingPointInfo& operating_point() const {
    return operating_point_;
  }

 private:
  StdVideoAV1ColorConfig color_config_;
  StdVideoAV1TimingInfo timing_info_;
  StdVideoAV1SequenceHeader sequence_header_;
  StdVideoEncodeAV1OperatingPointInfo operating_point_;
};

// What the AV1 picture syntax of one frame is built from.
struct Av1PictureParams {
  uint8_t order_hint = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  VkExtent2D coded_extent = {};
  uint32_t q_index = 0;
  uint32_t constant_q_index = 0;
};

// The AV1 picture information an encode command carries, with the reference
// information of its setup slot. The structures point at each other, so this
// is neither copied nor moved.
class Av1PictureInfo {
 public:
  explicit Av1PictureInfo(const Av1PictureParams& params);

  Av1PictureInfo(const Av1PictureInfo&) = delete;
  Av1PictureInfo& operator=(const Av1PictureInfo&) = delete;

  const VkVideoEncodeAV1PictureInfoKHR& info() const {
    return av1_picture_info_;
  }
  const VkVideoEncodeAV1DpbSlotInfoKHR& setup_slot_info() const {
    return setup_dpb_slot_info_;
  }

 private:
  StdVideoAV1TileInfo tile_info_;
  StdVideoAV1Quantization quantization_;
  StdVideoAV1LoopFilter loop_filter_;
  StdVideoAV1CDEF cdef_;
  StdVideoEncodeAV1PictureInfo picture_info_;
  VkVideoEncodeAV1PictureInfoKHR av1_picture_info_;
  StdVideoEncodeAV1ReferenceInfo setup_reference_info_;
  VkVideoEncodeAV1DpbSlotInfoKHR setup_dpb_slot_info_;
};

}  // namespace cuttlefish
