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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_ENCODER_PROVIDER_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_ENCODER_PROVIDER_H_

#include <memory>
#include <string>
#include <vector>

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Abstract interface for video encoder providers.
//
// Implementations register themselves with EncoderProviderRegistry at static
// initialization time. The CompositeEncoderFactory queries all registered
// providers and selects encoders based on priority and availability.
//
// Priority values:
//   0   = Software fallback (builtin WebRTC codecs)
//   100 = Hardware acceleration (e.g., NVENC, VAAPI)
//
// Thread safety: All methods must be safe to call from any thread.
class EncoderProvider {
 public:
  virtual ~EncoderProvider() = default;

  // Returns a human-readable name for this provider (e.g., "nvenc", "builtin").
  virtual std::string GetName() const = 0;

  // Returns the priority of this provider. Higher values are preferred.
  virtual int GetPriority() const = 0;

  // Returns true if this provider can create encoders on the current system.
  // May check for hardware availability, driver versions, etc.
  virtual bool IsAvailable() const = 0;

  // Returns the list of video formats this provider can encode.
  virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const = 0;

  // Creates an encoder for the given format.
  // Returns nullptr if the format is not supported or creation fails.
  virtual std::unique_ptr<webrtc::VideoEncoder> CreateEncoder(
      const webrtc::SdpVideoFormat& format) const = 0;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_ENCODER_PROVIDER_H_
