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

#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Global registry for DecoderProvider instances. Thread-safe.
class DecoderProviderRegistry {
 public:
  static DecoderProviderRegistry& Get();

  // Takes ownership. Skips unavailable providers.
  void Register(std::unique_ptr<DecoderProvider> provider);

  // Returns available providers sorted by priority (highest first).
  std::vector<DecoderProvider*> GetProviders() const;

 private:
  DecoderProviderRegistry() = default;
  ~DecoderProviderRegistry() = default;

  DecoderProviderRegistry(const DecoderProviderRegistry&) = delete;
  DecoderProviderRegistry& operator=(const DecoderProviderRegistry&) = delete;

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<DecoderProvider>> providers_;
};

template <typename T>
class DecoderProviderRegistrar {
 public:
  DecoderProviderRegistrar() {
    DecoderProviderRegistry::Get().Register(std::make_unique<T>());
  }
};

#define REGISTER_DECODER_PROVIDER(ProviderClass)                     \
  static ::cuttlefish::webrtc_streaming::DecoderProviderRegistrar<   \
      ProviderClass>                                                 \
      g_decoder_provider_registrar_##ProviderClass

}  // namespace webrtc_streaming
}  // namespace cuttlefish
