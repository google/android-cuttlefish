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

// Codec-specific configuration for NvencVideoEncoder.
// Each NVENC provider constructs one of these with the appropriate
// values for its codec. The encoder uses it without knowing which
// codec it is encoding.
//
// All fields are trivially destructible so this can be used as a
// namespace-scope constant without dynamic initialization concerns.

#pragma once

#include <stdint.h>
#include <type_traits>

#include <nvEncodeAPI.h>

#include "api/video/video_codec_type.h"
#include "api/video_codecs/video_encoder.h"

namespace cuttlefish {

struct NvencEncoderConfig {
  GUID codec_guid;
  GUID profile_guid;
  webrtc::VideoCodecType webrtc_codec_type;

  // Must point to a string literal (static lifetime).
  const char* implementation_name;

  // Resolution-based bitrate limits for WebRTC rate control.
  const webrtc::VideoEncoder::ResolutionBitrateLimits* bitrate_limits;
  size_t bitrate_limits_count;

  // Clamping range for SetRates(). min should be the technical
  // floor NVENC accepts (~50 kbps), not a quality floor — let
  // WebRTC's bandwidth estimator go as low as needed.
  int32_t min_bitrate_bps;
  int32_t max_bitrate_bps;

  // Called once during InitNvenc after preset loading.
  void (*apply_defaults)(NV_ENC_CONFIG* config);

  // Called per frame before nvEncEncodePicture. Must not clobber
  // encodePicFlags.
  void (*set_pic_params)(NV_ENC_PIC_PARAMS* params);
};

static_assert(std::is_trivially_destructible_v<NvencEncoderConfig>,
              "NvencEncoderConfig must be trivially destructible so "
              "it can be used as a namespace-scope constant");

}  // namespace cuttlefish
