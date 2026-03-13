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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_COMPOSITE_ENCODER_FACTORY_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_COMPOSITE_ENCODER_FACTORY_H_

#include <memory>

#include "api/video_codecs/video_encoder_factory.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider_registry.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Creates a VideoEncoderFactory that delegates to registered
// EncoderProviders.
//
// The factory merges supported formats from all available providers and
// creates encoders by trying providers in priority order until one
// succeeds.
//
// Uses the global EncoderProviderRegistry by default, but accepts an
// optional registry pointer for dependency injection in tests.
std::unique_ptr<webrtc::VideoEncoderFactory> CreateCompositeEncoderFactory(
    const EncoderProviderRegistry* registry = nullptr);

}  // namespace webrtc_streaming
}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_COMPOSITE_ENCODER_FACTORY_H_
