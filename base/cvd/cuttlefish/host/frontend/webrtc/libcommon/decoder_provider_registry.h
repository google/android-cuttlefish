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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_DECODER_PROVIDER_REGISTRY_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_DECODER_PROVIDER_REGISTRY_H_

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Thread-safe registry for DecoderProvider instances.
//
// Mirrors EncoderProviderRegistry for codec negotiation symmetry.
// Uses the "leaked pointer" singleton pattern recommended by the Google C++
// style guide.
class DecoderProviderRegistry {
 public:
  // Returns the global registry instance.
  static DecoderProviderRegistry& Get();

  // Registers a provider. Takes ownership of the provider.
  // Thread-safe. Typically called during static initialization.
  void Register(std::unique_ptr<DecoderProvider> provider);

  // Returns all available providers, sorted by priority (highest first).
  // Thread-safe. Returns a copy to avoid holding the lock.
  std::vector<DecoderProvider*> GetAvailableProviders() const;

  // Returns all registered providers (including unavailable ones).
  // Thread-safe. Useful for diagnostics.
  std::vector<DecoderProvider*> GetAllProviders() const;

 private:
  DecoderProviderRegistry() = default;
  ~DecoderProviderRegistry() = default;

  // Non-copyable
  DecoderProviderRegistry(const DecoderProviderRegistry&) = delete;
  DecoderProviderRegistry& operator=(const DecoderProviderRegistry&) = delete;

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<DecoderProvider>> providers_;
};

// Helper class for static registration of decoder providers.
// Used by the REGISTER_DECODER_PROVIDER macro.
template <typename T>
class DecoderProviderRegistrar {
 public:
  DecoderProviderRegistrar() {
    DecoderProviderRegistry::Get().Register(std::make_unique<T>());
  }
};

// Registers a DecoderProvider class at static initialization time.
// Usage: REGISTER_DECODER_PROVIDER(MyDecoderProvider);
#define REGISTER_DECODER_PROVIDER(ProviderClass)                     \
  static ::cuttlefish::webrtc_streaming::DecoderProviderRegistrar<   \
      ProviderClass>                                                 \
      g_decoder_provider_registrar_##ProviderClass

}  // namespace webrtc_streaming
}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_DECODER_PROVIDER_REGISTRY_H_
