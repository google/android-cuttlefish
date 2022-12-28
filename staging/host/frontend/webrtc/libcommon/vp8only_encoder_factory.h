/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <api/video_codecs/video_encoder_factory.h>
#include <api/video_codecs/video_encoder.h>

namespace cuttlefish {
namespace webrtc_streaming {

class VP8OnlyEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  VP8OnlyEncoderFactory(std::unique_ptr<webrtc::VideoEncoderFactory> inner);

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
      const webrtc::SdpVideoFormat& format) override;

  std::unique_ptr<EncoderSelectorInterface> GetEncoderSelector() const override;

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> inner_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
