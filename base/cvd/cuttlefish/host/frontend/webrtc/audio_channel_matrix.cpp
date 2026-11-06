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

#include "cuttlefish/host/frontend/webrtc/audio_channel_matrix.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/log/check.h"

#include "cuttlefish/host/frontend/webrtc/audio_settings.h"

namespace cuttlefish {
namespace {

constexpr float kMinus3dB = 0.7071f;  // Center and surround channels
constexpr float kMinus6dB = 0.5f;     // LFE and stereo-to-mono

constexpr uint8_t kMono = GetChannelsCount(AudioChannelsLayout::Mono);
constexpr uint8_t kStereo = GetChannelsCount(AudioChannelsLayout::Stereo);
constexpr uint8_t kSurround51 =
    GetChannelsCount(AudioChannelsLayout::Surround51);

// Maps each source channel to the destination channel of the same index.
std::vector<std::vector<float>> BuildDirectMappingMatrix(uint8_t dst_channels,
                                                         uint8_t src_channels,
                                                         float volume) {
  std::vector<std::vector<float>> matrix(
      dst_channels, std::vector<float>(src_channels, 0.0f));
  const size_t mapped_channels = std::min(dst_channels, src_channels);
  for (size_t i = 0; i < mapped_channels; ++i) {
    matrix[i][i] = volume;
  }
  return matrix;
}

}  // namespace

std::vector<std::vector<float>> BuildChannelMixingMatrix(uint8_t dst_channels,
                                                         uint8_t src_channels,
                                                         float volume,
                                                         float fade,
                                                         float balance) {
  CHECK(volume >= 0) << "volume can't be negative";
  CHECK(fade >= -1.0f && fade <= 1.0f) << "fade must be within [-1, 1]";
  CHECK(balance >= -1.0f && balance <= 1.0f)
      << "balance must be within [-1, 1]";

  // Keep the legacy behavior until this feature is stable.
  if (fade == 0.0f && balance == 0.0f) {
    return BuildDirectMappingMatrix(dst_channels, src_channels, volume);
  }

  // Attenuation of each cabin quadrant: moving the fader towards the front
  // attenuates the rear speakers only, and vice versa; same for balance.
  const float front_gain = fade >= 0.0f ? 1.0f : 1.0f + fade;
  const float rear_gain = fade <= 0.0f ? 1.0f : 1.0f - fade;
  const float left_gain = balance <= 0.0f ? 1.0f : 1.0f - balance;
  const float right_gain = balance >= 0.0f ? 1.0f : 1.0f + balance;

  const float fl_gain = volume * front_gain * left_gain;
  const float fr_gain = volume * front_gain * right_gain;
  const float fc_gain = volume * front_gain;
  const float rl_gain = volume * rear_gain * left_gain;
  const float rr_gain = volume * rear_gain * right_gain;

  if (dst_channels == kStereo) {
    if (src_channels == kSurround51) {
      // Source channel order: FL, FR, FC, LFE, RL, RR
      return {
          {fl_gain, 0.0f, kMinus3dB * fc_gain * left_gain,
           kMinus6dB * volume * left_gain, kMinus3dB * rl_gain, 0.0f},
          {0.0f, fr_gain, kMinus3dB * fc_gain * right_gain,
           kMinus6dB * volume * right_gain, 0.0f, kMinus3dB * rr_gain},
      };
    }
    if (src_channels == kStereo) {
      return {
          {fl_gain, 0.0f},
          {0.0f, fr_gain},
      };
    }
    if (src_channels == kMono) {
      return {
          {fl_gain},
          {fr_gain},
      };
    }
  }

  // A mono sink has a single speaker, so there is nothing to balance; only
  // the front/rear fader applies.
  if (dst_channels == kMono) {
    const float front = volume * front_gain;
    const float rear = volume * rear_gain;
    if (src_channels == kSurround51) {
      return {
          {kMinus3dB * front, kMinus3dB * front, front, kMinus6dB * volume,
           kMinus3dB * rear, kMinus3dB * rear},
      };
    }
    if (src_channels == kStereo) {
      return {
          {kMinus6dB * front, kMinus6dB * front},
      };
    }
    if (src_channels == kMono) {
      return {
          {front},
      };
    }
  }

  return BuildDirectMappingMatrix(dst_channels, src_channels, volume);
}

}  // namespace cuttlefish
