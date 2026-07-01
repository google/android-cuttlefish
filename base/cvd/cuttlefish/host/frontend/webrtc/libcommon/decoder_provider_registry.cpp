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

#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider_registry.h"

#include "android-base/logging.h"

namespace cuttlefish {
namespace webrtc_streaming {

DecoderProviderRegistry& DecoderProviderRegistry::Get() {
  static DecoderProviderRegistry instance;
  return instance;
}

void DecoderProviderRegistry::Register(
    std::unique_ptr<DecoderProvider> provider) {
  if (!provider->IsAvailable()) {
    LOG(INFO) << "Decoder provider " << provider->GetName()
              << " not available, skipping registration";
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  LOG(INFO) << "Registering decoder provider: "
            << provider->GetName()
            << " (priority=" << provider->GetPriority() << ")";
  providers_.push_back(std::move(provider));
}

std::vector<DecoderProvider*>
DecoderProviderRegistry::GetProviders() const {
  std::vector<DecoderProvider*> result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(providers_.size());
    for (const auto& provider : providers_) {
      result.push_back(provider.get());
    }
  }
  // Sort outside the lock — we're sorting our private copy
  std::sort(result.begin(), result.end(),
            [](const DecoderProvider* a, const DecoderProvider* b) {
              return *a < *b;
            });
  return result;
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
