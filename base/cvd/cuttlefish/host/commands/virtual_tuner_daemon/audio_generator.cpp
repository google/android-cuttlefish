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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <span>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

constexpr int16_t kNoiseAmplitude = 4000;
// A tuned station plays its tone plus a little static. The peaks add up to
// kNoiseAmplitude, so the output stays within the same range as plain noise.
constexpr double kToneAmplitude = 3400.0;
constexpr int16_t kStationNoiseAmplitude = 600;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Writes `sample` to every channel of the frame starting at `frame`.
void WriteFrame(std::span<int16_t> samples, size_t frame, int16_t sample) {
  for (size_t channel = 0; channel < kChannels; ++channel) {
    samples[frame + channel] = sample;
  }
}

}  // namespace

double StationToneHz(const TunerStateSnapshot& snapshot) {
  const uint32_t khz = snapshot.frequency_hz / 1000;
  if (khz == 0) {
    return 0.0;
  }
  double tone_hz = 0.0;
  switch (snapshot.band) {
    case RadioBand::AM:
      tone_hz = 300.0 + 0.5 * (std::max<uint32_t>(khz, 530) - 530);
      break;
    case RadioBand::FM:
      tone_hz = 1000.0 + (std::max<uint32_t>(khz, 87500) - 87500) / 10.0;
      break;
    case RadioBand::DAB:
      return 5000.0 + (std::max<uint32_t>(khz, 174000) - 174000) / 50.0;
    default:
      return 0.0;
  }
  return tone_hz + 1000.0 * snapshot.hd_subchannel;
}

AudioGenerator::AudioGenerator() : rng_(std::random_device()()) {}

void AudioGenerator::GenerateChunk(std::span<int16_t> samples,
                                   const TunerStateSnapshot& snapshot) {
  if (!snapshot.is_playing) {
    std::fill(samples.begin(), samples.end(), 0);
    return;
  }

  // Plays StationToneHz() with a little static when tuned to a station, or
  // white noise when no station is broadcasting on the current frequency.
  const double tone_hz = StationToneHz(snapshot);
  if (tone_hz <= 0.0) {
    std::uniform_int_distribution<int> noise(-kNoiseAmplitude, kNoiseAmplitude);
    for (size_t frame = 0; frame + kChannels <= samples.size();
         frame += kChannels) {
      WriteFrame(samples, frame, static_cast<int16_t>(noise(rng_)));
    }
    return;
  }

  std::uniform_int_distribution<int> noise(-kStationNoiseAmplitude,
                                           kStationNoiseAmplitude);
  const double phase_step = kTwoPi * tone_hz / kSampleRate;
  for (size_t frame = 0; frame + kChannels <= samples.size();
       frame += kChannels) {
    const int tone =
        static_cast<int>(std::lround(kToneAmplitude * std::sin(phase_)));
    WriteFrame(samples, frame, static_cast<int16_t>(tone + noise(rng_)));
    phase_ = std::fmod(phase_ + phase_step, kTwoPi);
  }
}

}  // namespace virtualtuner
}  // namespace cuttlefish
