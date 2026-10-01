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

#include <type_traits>

#include "api/video/video_codec_type.h"
#include "api/video_codecs/video_encoder.h"

namespace cuttlefish {

// Codec-specific configuration for VulkanVideoEncoder, defined as a
// namespace-scope constant.
struct VulkanEncoderConfig {
  webrtc::VideoCodecType webrtc_codec_type;

  // Must point to a string literal (static lifetime).
  const char* implementation_name;

  // Resolution-based bitrate limits for WebRTC rate control.
  const webrtc::VideoEncoder::ResolutionBitrateLimits* bitrate_limits;
  size_t bitrate_limits_count;
};

static_assert(std::is_trivially_destructible_v<VulkanEncoderConfig>,
              "VulkanEncoderConfig must be trivially destructible so "
              "it can be used as a namespace-scope constant");

}  // namespace cuttlefish
