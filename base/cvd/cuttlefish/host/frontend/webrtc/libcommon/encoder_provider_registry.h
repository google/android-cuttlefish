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

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Global registry for EncoderProvider instances. Thread-safe.
class EncoderProviderRegistry {
 public:
  static EncoderProviderRegistry& Get();

  // Takes ownership. Skips unavailable providers.
  void Register(std::unique_ptr<EncoderProvider> provider);

  // Returns available providers sorted by priority (highest first).
  std::vector<EncoderProvider*> GetProviders() const;

 private:
  EncoderProviderRegistry() = default;
  ~EncoderProviderRegistry() = default;

  EncoderProviderRegistry(const EncoderProviderRegistry&) = delete;
  EncoderProviderRegistry& operator=(const EncoderProviderRegistry&) = delete;

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<EncoderProvider>> providers_;
};

template <typename T>
class EncoderProviderRegistrar {
 public:
  EncoderProviderRegistrar() {
    EncoderProviderRegistry::Get().Register(std::make_unique<T>());
  }
};

#define REGISTER_ENCODER_PROVIDER(ProviderClass)                     \
  static ::cuttlefish::webrtc_streaming::EncoderProviderRegistrar<   \
      ProviderClass>                                                 \
      g_encoder_provider_registrar_##ProviderClass

}  // namespace webrtc_streaming
}  // namespace cuttlefish
