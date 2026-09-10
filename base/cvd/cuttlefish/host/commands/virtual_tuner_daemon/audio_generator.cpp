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

#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>

#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

constexpr int16_t kNoiseAmplitude = 4000;

}  // namespace

AudioGenerator::AudioGenerator() : rng_(std::random_device()()) {}

void AudioGenerator::GenerateChunk(std::span<int16_t> samples,
                                   const TunerStateSnapshot& snapshot) {
  if (!snapshot.is_playing) {
    std::fill(samples.begin(), samples.end(), 0);
    return;
  }

  std::uniform_int_distribution<int> noise(-kNoiseAmplitude, kNoiseAmplitude);
  for (size_t frame = 0; frame + kChannels <= samples.size();
       frame += kChannels) {
    const auto sample = static_cast<int16_t>(noise(rng_));
    for (size_t channel = 0; channel < kChannels; ++channel) {
      samples[frame + channel] = sample;
    }
  }
}

}  // namespace virtualtuner
}  // namespace cuttlefish
