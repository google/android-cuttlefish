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
#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider_registry.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_capabilities.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/stub_decoder.h"

namespace cuttlefish {
namespace webrtc_streaming {
namespace {

// AV1 NVENC decoder stub provider.
//
// Advertises AV1 decoding support so WebRTC's SDP negotiation succeeds.
// Cuttlefish never decodes video (the browser does), so this returns a
// StubDecoder. Availability is gated on the GPU supporting AV1 NVENC
// encoding — there's no point offering AV1 in SDP if we can't encode it.
class Av1NvencDecoderProvider : public DecoderProvider {
 public:
  Av1NvencDecoderProvider() {
    available_ = IsNvencCodecSupported(NV_ENC_CODEC_AV1_GUID);
  }

  std::string GetName() const override { return "nvenc_av1_stub"; }
  int GetPriority() const override { return 101; }
  bool IsAvailable() const override { return available_; }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats()
      const override {
    if (!available_) return {};
    return {{"AV1", {{"profile", "0"}}}};
  }

  std::unique_ptr<webrtc::VideoDecoder> CreateDecoder(
      const webrtc::SdpVideoFormat& format) const override {
    if (!available_) return nullptr;
    if (!absl::EqualsIgnoreCase(format.name, "AV1")) return nullptr;
    return std::make_unique<StubDecoder>();
  }

 private:
  bool available_ = false;
};

}  // namespace

REGISTER_DECODER_PROVIDER(Av1NvencDecoderProvider);

}  // namespace webrtc_streaming
}  // namespace cuttlefish
