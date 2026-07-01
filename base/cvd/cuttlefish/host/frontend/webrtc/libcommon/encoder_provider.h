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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Interface for pluggable video encoder backends.
//
// Providers register themselves at static init time via
// REGISTER_ENCODER_PROVIDER. The CompositeEncoderFactory queries
// providers in priority order (higher = preferred).
//
// Priority: 0 = software fallback, 100+ = hardware acceleration.
class EncoderProvider {
 public:
  virtual ~EncoderProvider() = default;

  virtual std::string GetName() const = 0;
  virtual int GetPriority() const = 0;
  virtual bool IsAvailable() const = 0;
  virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const = 0;
  virtual Result<std::unique_ptr<webrtc::VideoEncoder>> CreateEncoder(
      const webrtc::SdpVideoFormat& format) const = 0;

  // Descending: higher priority sorts first.
  bool operator<(const EncoderProvider& other) const {
    return other.GetPriority() < GetPriority();
  }
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
