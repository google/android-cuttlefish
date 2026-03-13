// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Codec-specific configuration for NvencVideoEncoder.
// Each NVENC provider constructs one of these with the appropriate
// values for its codec. The encoder uses it without knowing which
// codec it is encoding.
//
// This struct uses only trivially destructible types so that config
// constants can be declared at namespace scope without violating the
// Google C++ Style Guide's prohibition on dynamic initialization of
// global variables.

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_ENCODER_CONFIG_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_ENCODER_CONFIG_H_

#include <cstdint>
#include <type_traits>

#include <nvEncodeAPI.h>

#include "api/video/video_codec_type.h"
#include "api/video_codecs/video_encoder.h"

namespace cuttlefish {

struct NvencEncoderConfig {
  // NVENC codec GUID (e.g., NV_ENC_CODEC_H264_GUID).
  GUID codec_guid;

  // NVENC profile GUID (e.g., NV_ENC_H264_PROFILE_HIGH_GUID).
  GUID profile_guid;

  // WebRTC codec type for EncodedImage metadata.
  webrtc::VideoCodecType webrtc_codec_type;

  // Human-readable name for EncoderInfo (e.g., "NvencAv1").
  // Must point to a string literal or other storage with static
  // lifetime.
  const char* implementation_name;

  // Resolution-based bitrate limits for WebRTC rate control.
  // Points to a static array defined in the provider file.
  // Format per entry: {frame_size_pixels, min_start_bps, min_bps,
  // max_bps}.
  const webrtc::VideoEncoder::ResolutionBitrateLimits* bitrate_limits;
  size_t bitrate_limits_count;

  // Technical minimum/maximum bitrate for SetRates() clamping.
  // min_bitrate_bps should be set to the lowest bitrate at which
  // NVENC will accept the configuration (~50 kbps), NOT a quality
  // floor. WebRTC's bandwidth estimator must be free to drop the
  // bitrate as low as needed to keep the stream alive on constrained
  // networks. Quality management is handled by resolution_bitrate_
  // limits (above), which tell WebRTC to downscale the video rather
  // than starve the encoder.
  int32_t min_bitrate_bps;
  int32_t max_bitrate_bps;

  // Called once during InitNvenc, after preset loading and common
  // rate control config. Sets codec-specific encode config fields
  // (e.g., repeatSPSPPS for H.264, tile config for AV1).
  void (*apply_defaults)(NV_ENC_CONFIG* config);

  // Called on every frame before the keyframe check and before
  // nvEncEncodePicture. Sets per-frame codec-specific picture
  // parameters (e.g., tile counts for AV1). Must not clobber
  // encodePicFlags. May be a no-op for codecs that need no
  // per-frame params.
  void (*set_pic_params)(NV_ENC_PIC_PARAMS* params);
};

static_assert(std::is_trivially_destructible_v<NvencEncoderConfig>,
              "NvencEncoderConfig must be trivially destructible so "
              "it can be used as a namespace-scope constant");

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_ENCODER_CONFIG_H_
