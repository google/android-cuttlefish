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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_ENCODER_PROVIDER_REGISTRY_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_ENCODER_PROVIDER_REGISTRY_H_

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Thread-safe registry for EncoderProvider instances.
//
// Uses the "leaked pointer" singleton pattern.
//
// Example usage:
//   // At static init time (via REGISTER_ENCODER_PROVIDER macro)
//   EncoderProviderRegistry::Get().Register(
//       std::make_unique<MyEncoderProvider>());
//
//   // At runtime
//   for (auto* provider :
//        EncoderProviderRegistry::Get().GetAvailableProviders()) {
//     // Use provider...
//   }
class EncoderProviderRegistry {
 public:
  // Returns the global registry instance.
  static EncoderProviderRegistry& Get();

  // Registers a provider. Takes ownership of the provider.
  // Thread-safe. Typically called during static initialization.
  void Register(std::unique_ptr<EncoderProvider> provider);

  // Returns all available providers, sorted by priority (highest first).
  // Thread-safe. Returns a copy to avoid holding the lock.
  std::vector<EncoderProvider*> GetAvailableProviders() const;

  // Returns all registered providers (including unavailable ones).
  // Thread-safe. Useful for diagnostics.
  std::vector<EncoderProvider*> GetAllProviders() const;

 private:
  EncoderProviderRegistry() = default;
  ~EncoderProviderRegistry() = default;

  // Non-copyable
  EncoderProviderRegistry(const EncoderProviderRegistry&) = delete;
  EncoderProviderRegistry& operator=(const EncoderProviderRegistry&) = delete;

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<EncoderProvider>> providers_;
};

// Helper class for static registration of encoder providers.
// Used by the REGISTER_ENCODER_PROVIDER macro.
template <typename T>
class EncoderProviderRegistrar {
 public:
  EncoderProviderRegistrar() {
    EncoderProviderRegistry::Get().Register(std::make_unique<T>());
  }
};

// Registers an EncoderProvider class at static initialization time.
// Usage: REGISTER_ENCODER_PROVIDER(MyEncoderProvider);
#define REGISTER_ENCODER_PROVIDER(ProviderClass)                     \
  static ::cuttlefish::webrtc_streaming::EncoderProviderRegistrar<   \
      ProviderClass>                                                 \
      g_encoder_provider_registrar_##ProviderClass

}  // namespace webrtc_streaming
}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_ENCODER_PROVIDER_REGISTRY_H_
