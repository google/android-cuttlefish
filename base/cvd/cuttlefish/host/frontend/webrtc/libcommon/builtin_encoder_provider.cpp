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

#include <memory>
#include <string>
#include <vector>

#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider_registry.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

// Encoder provider that wraps WebRTC's builtin encoder factory.
//
// Provides software codecs (VP8, VP9, AV1) as a fallback when hardware
// encoders are not available. Priority 0 ensures this is used only when
// no higher-priority provider can handle the format.
class BuiltinEncoderProvider : public EncoderProvider {
 public:
  BuiltinEncoderProvider()
      : factory_(webrtc::CreateBuiltinVideoEncoderFactory()) {}

  std::string GetName() const override { return "builtin"; }

  int GetPriority() const override { return 0; }

  bool IsAvailable() const override { return true; }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return factory_->GetSupportedFormats();
  }

  std::unique_ptr<webrtc::VideoEncoder> CreateEncoder(
      const webrtc::SdpVideoFormat& format) const override {
    return factory_->CreateVideoEncoder(format);
  }

 private:
  // Cached factory instance - created once at construction time.
  // This avoids repeated factory creation overhead.
  std::unique_ptr<webrtc::VideoEncoderFactory> factory_;
};

}  // namespace

// Static registration - provider is registered during static initialization.
REGISTER_ENCODER_PROVIDER(BuiltinEncoderProvider);

}  // namespace webrtc_streaming
}  // namespace cuttlefish
