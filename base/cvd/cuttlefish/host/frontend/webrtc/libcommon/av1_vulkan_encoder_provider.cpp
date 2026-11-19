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

#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider_registry.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_encoder_config.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_video_encoder.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace webrtc_streaming {
namespace {

// {frame_size_pixels, min_start_bps, min_bps, max_bps}, the same limits as the
// NVENC AV1 provider.
const webrtc::VideoEncoder::ResolutionBitrateLimits kAv1BitrateLimits[] = {
    {320 * 240, 80000, 40000, 800000},
    {640 * 480, 200000, 80000, 2000000},
    {1280 * 720, 500000, 200000, 4000000},
    {1600 * 900, 900000, 350000, 7000000},
    {1920 * 1080, 1200000, 500000, 10000000},
    {2560 * 1440, 2500000, 1000000, 18000000},
    {3840 * 2160, 5000000, 2000000, 30000000},
};

const VulkanEncoderConfig kAv1Config = {
    .webrtc_codec_type = webrtc::kVideoCodecAV1,
    .implementation_name = "VulkanAv1",
    .bitrate_limits = kAv1BitrateLimits,
    .bitrate_limits_count = std::size(kAv1BitrateLimits),
    .min_bitrate_bps = 50000,     // 50 kbps technical minimum
    .max_bitrate_bps = 12000000,  // 12 Mbps
    // A fifth of a second of buffer. Longer buffers let the driver overshoot
    // a bitrate change for as long as they last, which a live stream feels.
    .virtual_buffer_size_ms = 200,
    .initial_virtual_buffer_size_ms = 200,
    // Clamped to the range the driver reports.
    .quality_level = 3,
};

// Priority 100: below NVENC (101), above the software encoders (0).
class Av1VulkanEncoderProvider : public EncoderProvider {
 public:
  Av1VulkanEncoderProvider() : available_(IsVulkanAv1EncodeSupported()) {}

  std::string GetName() const override { return "vulkan_av1"; }
  int GetPriority() const override { return 100; }
  bool IsAvailable() const override { return available_; }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    if (!available_) {
      return {};
    }
    return {{"AV1", {{"profile", "0"}}}};
  }

  Result<std::unique_ptr<webrtc::VideoEncoder>> CreateEncoder(
      const webrtc::SdpVideoFormat& format) const override {
    if (!available_) {
      return CF_ERR("Vulkan AV1 encode not available");
    }
    CF_EXPECTF(absl::EqualsIgnoreCase(format.name, "AV1"),
               "Unsupported format: '{}'", format.name);
    return std::make_unique<VulkanVideoEncoder>(kAv1Config, format);
  }

 private:
  bool available_ = false;
};

}  // namespace

REGISTER_ENCODER_PROVIDER(Av1VulkanEncoderProvider);

}  // namespace webrtc_streaming
}  // namespace cuttlefish
