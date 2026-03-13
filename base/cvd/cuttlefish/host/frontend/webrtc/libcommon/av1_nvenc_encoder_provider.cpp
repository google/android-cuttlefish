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

#include <memory>
#include <string>
#include <vector>

#include <nvEncodeAPI.h>

#include "absl/strings/match.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider_registry.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_capabilities.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_encoder_config.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_video_encoder.h"
#include "rtc_base/logging.h"

namespace cuttlefish {
namespace webrtc_streaming {
namespace {

// Codec-specific NVENC configuration for AV1.

void Av1ApplyDefaults(NV_ENC_CONFIG* config) {
  auto& av1 = config->encodeCodecConfig.av1Config;
  av1.outputAnnexBFormat = 0;   // OBU format required for WebRTC.
  av1.disableSeqHdr = 0;
  av1.repeatSeqHdr = 1;         // Keyframes must be self-contained.
  av1.chromaFormatIDC = 1;      // YUV 4:2:0.
  av1.inputPixelBitDepthMinus8 = 0;   // 8-bit input.
  av1.pixelBitDepthMinus8 = 0;        // 8-bit output.
  av1.numTileColumns = 1;
  av1.numTileRows = 1;
  av1.tier = NV_ENC_TIER_AV1_0;
  av1.level = NV_ENC_LEVEL_AV1_AUTOSELECT;
  av1.idrPeriod = config->gopLength;
}

void Av1SetPicParams(NV_ENC_PIC_PARAMS* params) {
  // AV1 requires tile configuration on every frame.
  params->codecPicParams.av1PicParams.numTileColumns = 1;
  params->codecPicParams.av1PicParams.numTileRows = 1;
}

// AV1 achieves similar quality at ~60-70% of H.264 bitrate.
// These are initial estimates and should be tuned with real
// encoding measurements on target hardware (L4/L40).
const webrtc::VideoEncoder::ResolutionBitrateLimits
    kAv1BitrateLimits[] = {
        {320 * 240,    80000,   40000,   800000},
        {640 * 480,   200000,   80000,  2000000},
        {1280 * 720,  500000,  200000,  4000000},
        {1600 * 900,  900000,  350000,  7000000},
        {1920 * 1080, 1200000, 500000, 10000000},
        {2560 * 1440, 2500000, 1000000, 18000000},
        {3840 * 2160, 5000000, 2000000, 30000000},
};

const NvencEncoderConfig kAv1Config = {
    .codec_guid = NV_ENC_CODEC_AV1_GUID,
    .profile_guid = NV_ENC_AV1_PROFILE_MAIN_GUID,
    .webrtc_codec_type = webrtc::kVideoCodecAV1,
    .implementation_name = "NvencAv1",
    .bitrate_limits = kAv1BitrateLimits,
    .bitrate_limits_count = std::size(kAv1BitrateLimits),
    .min_bitrate_bps = 50000,       // 50 kbps technical minimum
    .max_bitrate_bps = 12000000,    // 12 Mbps
    .apply_defaults = Av1ApplyDefaults,
    .set_pic_params = Av1SetPicParams,
};

// AV1 NVENC encoder provider (public repo).
//
// Priority 101 ensures AV1 is preferred over other codec providers in
// SDP offers when multiple are available. AV1 offers better compression
// efficiency at comparable quality.
class Av1NvencEncoderProvider : public EncoderProvider {
 public:
  Av1NvencEncoderProvider() {
    available_ = IsNvencCodecSupported(NV_ENC_CODEC_AV1_GUID);
    RTC_LOG(LS_INFO) << "Av1NvencEncoderProvider: "
                     << (available_ ? "available" : "not available");
  }

  std::string GetName() const override { return "nvenc_av1"; }
  int GetPriority() const override { return 101; }
  bool IsAvailable() const override { return available_; }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats()
      const override {
    if (!available_) return {};
    return {{"AV1", {{"profile", "0"}}}};
  }

  std::unique_ptr<webrtc::VideoEncoder> CreateEncoder(
      const webrtc::SdpVideoFormat& format) const override {
    if (!available_) return nullptr;
    if (!absl::EqualsIgnoreCase(format.name, "AV1")) return nullptr;
    return std::make_unique<NvencVideoEncoder>(kAv1Config, format);
  }

 private:
  bool available_ = false;
};

}  // namespace

REGISTER_ENCODER_PROVIDER(Av1NvencEncoderProvider);

}  // namespace webrtc_streaming
}  // namespace cuttlefish
