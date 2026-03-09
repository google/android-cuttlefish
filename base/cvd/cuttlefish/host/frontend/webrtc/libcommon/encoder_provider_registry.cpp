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

#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider_registry.h"

#include "rtc_base/logging.h"

namespace cuttlefish {
namespace webrtc_streaming {

EncoderProviderRegistry& EncoderProviderRegistry::Get() {
  // Leaked pointer singleton and intentionally never deleted.
  // This pattern is recommended for singletons
  // that must survive until program termination, avoiding static destruction
  // order issues with providers registered during static initialization.
  static EncoderProviderRegistry* instance = new EncoderProviderRegistry();
  return *instance;
}

void EncoderProviderRegistry::Register(std::unique_ptr<EncoderProvider> provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  RTC_LOG(LS_INFO) << "Registering encoder provider: " << provider->GetName()
                   << " (priority=" << provider->GetPriority() << ")";
  providers_.push_back(std::move(provider));
}

std::vector<EncoderProvider*> EncoderProviderRegistry::GetAvailableProviders() const {
  std::lock_guard<std::mutex> lock(mutex_);

  // Build filtered list of available providers
  std::vector<EncoderProvider*> result;
  result.reserve(providers_.size());
  for (const auto& provider : providers_) {
    if (provider->IsAvailable()) {
      result.push_back(provider.get());
    }
  }

  // Sort the COPY by priority (highest first), not the source vector.
  // This is a thread-safety fix: sorting the source would violate const
  // correctness and could cause data races if another thread is iterating.
  std::sort(result.begin(), result.end(),
            [](const EncoderProvider* a, const EncoderProvider* b) {
              return a->GetPriority() > b->GetPriority();
            });

  return result;
}

std::vector<EncoderProvider*> EncoderProviderRegistry::GetAllProviders() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<EncoderProvider*> result;
  result.reserve(providers_.size());
  for (const auto& provider : providers_) {
    result.push_back(provider.get());
  }

  return result;
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
