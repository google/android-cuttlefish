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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_STUB_DECODER_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_STUB_DECODER_H_

#include "api/video_codecs/video_decoder.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"

namespace cuttlefish {
namespace webrtc_streaming {

// A stub decoder that does nothing. Cuttlefish only sends video;
// the browser decodes it. This stub satisfies WebRTC's codec
// negotiation requirement.
class StubDecoder : public webrtc::VideoDecoder {
 public:
  bool Configure(const Settings& settings) override { return true; }

  int32_t Decode(const webrtc::EncodedImage& input_image,
                 bool missing_frames,
                 int64_t render_time_ms) override {
    RTC_LOG(LS_WARNING) << "StubDecoder::Decode called - unexpected";
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override {
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override { return WEBRTC_VIDEO_CODEC_OK; }

  DecoderInfo GetDecoderInfo() const override {
    DecoderInfo info;
    info.implementation_name = "StubDecoder";
    info.is_hardware_accelerated = false;
    return info;
  }
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_STUB_DECODER_H_
