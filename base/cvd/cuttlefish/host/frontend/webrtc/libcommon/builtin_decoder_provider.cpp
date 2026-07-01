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

#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_decoder_factory.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider_registry.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

// Software fallback decoder (VP8 via libvpx, AV1 via dav1d).
class BuiltinDecoderProvider : public DecoderProvider {
 public:
  BuiltinDecoderProvider()
      : factory_(webrtc::CreateBuiltinVideoDecoderFactory()) {}

  std::string GetName() const override { return "builtin"; }

  int GetPriority() const override { return 0; }

  bool IsAvailable() const override { return true; }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> result;
    for (const webrtc::SdpVideoFormat& format :
         factory_->GetSupportedFormats()) {
      if (format.name == "VP8" || format.name == "AV1") {
        result.push_back(format);
      }
    }
    return result;
  }

  Result<std::unique_ptr<webrtc::VideoDecoder>> CreateDecoder(
      const webrtc::SdpVideoFormat& format) const override {
    std::unique_ptr<webrtc::VideoDecoder> decoder =
        factory_->CreateVideoDecoder(format);
    CF_EXPECT(decoder != nullptr,
              "Builtin decoder failed to create " << format.name);
    return decoder;
  }

 private:
  std::unique_ptr<webrtc::VideoDecoderFactory> factory_;
};

}  // namespace

REGISTER_DECODER_PROVIDER(BuiltinDecoderProvider);

}  // namespace webrtc_streaming
}  // namespace cuttlefish
