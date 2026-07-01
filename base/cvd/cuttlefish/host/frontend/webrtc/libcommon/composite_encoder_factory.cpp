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

#include "cuttlefish/host/frontend/webrtc/libcommon/composite_encoder_factory.h"

#include <set>
#include <string>

#include "android-base/logging.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

class CompositeEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  explicit CompositeEncoderFactory(const EncoderProviderRegistry* registry)
      : registry_(registry ? registry : &EncoderProviderRegistry::Get()) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> all_formats;
    std::set<std::string> seen;

    // Deduplicate by string representation; higher-priority first.
    for (const EncoderProvider* provider : registry_->GetProviders()) {
      for (const webrtc::SdpVideoFormat& format : provider->GetSupportedFormats()) {
        std::string key = format.ToString();
        if (seen.insert(key).second) {
          all_formats.push_back(format);
        }
      }
    }

    return all_formats;
  }

  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
      const webrtc::SdpVideoFormat& format) override {
    for (EncoderProvider* provider : registry_->GetProviders()) {
      Result<std::unique_ptr<webrtc::VideoEncoder>> result =
          provider->CreateEncoder(format);
      if (result.ok() && *result) {
        LOG(INFO) << "Created encoder for " << format.ToString()
                  << " using provider: " << provider->GetName();
        return std::move(*result);
      }
    }

    LOG(ERROR) << "No encoder provider could create encoder for: "
               << format.ToString();
    return nullptr;
  }

  CodecSupport QueryCodecSupport(
      const webrtc::SdpVideoFormat& format,
      absl::optional<std::string> scalability_mode) const override {
    for (const EncoderProvider* provider : registry_->GetProviders()) {
      for (const webrtc::SdpVideoFormat& supported : provider->GetSupportedFormats()) {
        if (format.IsSameCodec(supported)) {
          return {.is_supported = true};
        }
      }
    }
    return {.is_supported = false};
  }

 private:
  const EncoderProviderRegistry* registry_;
};

}  // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> CreateCompositeEncoderFactory(
    const EncoderProviderRegistry* registry) {
  return std::make_unique<CompositeEncoderFactory>(registry);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
