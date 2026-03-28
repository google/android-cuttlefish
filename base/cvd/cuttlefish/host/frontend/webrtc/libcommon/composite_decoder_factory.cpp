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

#include "cuttlefish/host/frontend/webrtc/libcommon/composite_decoder_factory.h"

#include <set>
#include <string>

#include "rtc_base/logging.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

class CompositeDecoderFactory : public webrtc::VideoDecoderFactory {
 public:
  explicit CompositeDecoderFactory(const DecoderProviderRegistry* registry)
      : registry_(registry ? registry : &DecoderProviderRegistry::Get()) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> all_formats;
    std::set<std::string> seen;

    // Collect formats from all available providers, deduplicating by string
    // representation. Higher-priority providers are iterated first.
    for (const DecoderProvider* provider : registry_->GetAvailableProviders()) {
      for (const auto& format : provider->GetSupportedFormats()) {
        std::string key = format.ToString();
        if (seen.insert(key).second) {
          all_formats.push_back(format);
        }
      }
    }

    return all_formats;
  }

  std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(
      const webrtc::SdpVideoFormat& format) override {
    // Try providers in priority order until one returns a decoder
    for (DecoderProvider* provider : registry_->GetAvailableProviders()) {
      auto decoder = provider->CreateDecoder(format);
      if (decoder) {
        RTC_LOG(LS_INFO) << "Created decoder for " << format.ToString()
                         << " using provider: " << provider->GetName();
        return decoder;
      }
    }

    RTC_LOG(LS_WARNING) << "No decoder provider could create decoder for: "
                        << format.ToString();
    return nullptr;
  }

  CodecSupport QueryCodecSupport(
      const webrtc::SdpVideoFormat& format,
      bool reference_scaling) const override {
    // Check if any provider can create this codec
    for (const DecoderProvider* provider : registry_->GetAvailableProviders()) {
      for (const auto& supported : provider->GetSupportedFormats()) {
        if (format.IsSameCodec(supported)) {
          return {.is_supported = true};
        }
      }
    }
    return {.is_supported = false};
  }

 private:
  const DecoderProviderRegistry* registry_;
};

}  // namespace

std::unique_ptr<webrtc::VideoDecoderFactory> CreateCompositeDecoderFactory(
    const DecoderProviderRegistry* registry) {
  return std::make_unique<CompositeDecoderFactory>(registry);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
