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

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>

#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {

inline constexpr size_t kSampleRate = 48000;
inline constexpr size_t kChannels = 2;
inline constexpr size_t kBytesPerSample = sizeof(int16_t);
inline constexpr size_t kFrameSizeBytes = kChannels * kBytesPerSample;

inline constexpr size_t kChunkFrames = 4096;
inline constexpr size_t kChunkSizeBytes = kChunkFrames * kFrameSizeBytes;

class AudioGenerator {
 public:
  AudioGenerator();

  void GenerateChunk(std::span<int16_t> samples,
                     const TunerStateSnapshot& snapshot);

 private:
  std::minstd_rand rng_;
};

}  // namespace virtualtuner
}  // namespace cuttlefish
