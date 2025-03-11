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

#include "host/frontend/webrtc/lib/vp8only_encoder_factory.h"

namespace cuttlefish {
namespace webrtc_streaming {
VP8OnlyEncoderFactory::VP8OnlyEncoderFactory(
    std::unique_ptr<webrtc::VideoEncoderFactory> inner)
    : inner_(std::move(inner)) {}

std::vector<webrtc::SdpVideoFormat> VP8OnlyEncoderFactory::GetSupportedFormats()
    const {
  std::vector<webrtc::SdpVideoFormat> ret;
  // Allow only VP8
  for (auto& format : inner_->GetSupportedFormats()) {
    if (format.name == "VP8") {
      ret.push_back(format);
    }
  }
  return ret;
}

webrtc::VideoEncoderFactory::CodecInfo VP8OnlyEncoderFactory::QueryVideoEncoder(
    const webrtc::SdpVideoFormat& format) const {
  return inner_->QueryVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoder> VP8OnlyEncoderFactory::CreateVideoEncoder(
    const webrtc::SdpVideoFormat& format) {
  return inner_->CreateVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoderFactory::EncoderSelectorInterface>
VP8OnlyEncoderFactory::GetEncoderSelector() const {
  return inner_->GetEncoderSelector();
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
