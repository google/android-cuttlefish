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

#include <stddef.h>
#include <stdint.h>

#include <type_traits>

#include "api/video/video_codec_type.h"
#include "api/video_codecs/video_encoder.h"

namespace cuttlefish {

// Codec-specific configuration for VulkanVideoEncoder. Each Vulkan provider
// defines one as a namespace-scope constant.
struct VulkanEncoderConfig {
  webrtc::VideoCodecType webrtc_codec_type;

  // Must point to a string literal (static lifetime).
  const char* implementation_name;

  // Resolution-based bitrate limits for WebRTC rate control.
  const webrtc::VideoEncoder::ResolutionBitrateLimits* bitrate_limits;
  size_t bitrate_limits_count;

  // Clamping range for SetRates(). min is the technical floor, not a quality
  // floor, so WebRTC's bandwidth estimator keeps its full range.
  int32_t min_bitrate_bps;
  int32_t max_bitrate_bps;

  // Coded picture buffer size and its initial fullness, in milliseconds of
  // the target bitrate. The buffer size must not be zero: a driver handed a
  // zero sized buffer has nothing to pace against.
  uint32_t virtual_buffer_size_ms;
  uint32_t initial_virtual_buffer_size_ms;

  // Encode quality level, clamped to the range the driver reports.
  uint32_t quality_level;
};

static_assert(std::is_trivially_destructible_v<VulkanEncoderConfig>,
              "VulkanEncoderConfig must be trivially destructible so "
              "it can be used as a namespace-scope constant");

}  // namespace cuttlefish
