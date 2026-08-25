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
#include <array>

#include "cuttlefish/host/frontend/webrtc/audio_settings.h"

namespace cuttlefish {
namespace {

constexpr float kMinus3dB = 0.7071f;  // 1 / sqrt(2) for Center & Surround
constexpr float kMinus6dB = 0.5000f;  // 1 / 2 for LFE (Subwoofer) and Stereo-to-Mono

// Standard ITU-R BS.775 base downmix coefficients
std::vector<std::vector<float>> GetItuDownmixMatrix(uint8_t dst_channels,
                                                    uint8_t src_channels) {
  constexpr uint8_t kMono = GetChannelsCount(AudioChannelsLayout::Mono);        // 1
  constexpr uint8_t kStereo = GetChannelsCount(AudioChannelsLayout::Stereo);    // 2
  constexpr uint8_t kSurround51 =
      GetChannelsCount(AudioChannelsLayout::Surround51);                        // 6

  // 1. Same layout (1->1, 2->2, 6->6) -> Identity Matrix
  if (dst_channels == src_channels) {
    std::vector<std::vector<float>> matrix(
        dst_channels, std::vector<float>(src_channels, 0.0f));
    for (size_t i = 0; i < dst_channels; ++i) {
      matrix[i][i] = 1.0f;
    }
    return matrix;
  }

  // 2. 5.1 Surround -> Stereo (6 -> 2) [ITU-R BS.775 §2.2]
  // Source layout: [FL, FR, FC, LFE, RL, RR]
  if (dst_channels == kStereo && src_channels == kSurround51) {
    return {
        {1.0f, 0.0f, kMinus3dB, kMinus6dB, kMinus3dB, 0.0f},       // Left
        {0.0f, 1.0f, kMinus3dB, kMinus6dB, 0.0f, kMinus3dB},      // Right
    };
  }

  // 3. Mono -> Stereo (1 -> 2) [Center voice to both Left & Right]
  if (dst_channels == kStereo && src_channels == kMono) {
    return {
        {1.0f},  // Left
        {1.0f},  // Right
    };
  }

  // 4. Stereo -> Mono (2 -> 1) [Equal sum of Left & Right]
  if (dst_channels == kMono && src_channels == kStereo) {
    return {
        {kMinus6dB, kMinus6dB},
    };
  }

  // 5. 5.1 Surround -> Mono (6 -> 1) [ITU-R BS.775 §2.1]
  if (dst_channels == kMono && src_channels == kSurround51) {
    return {
        {kMinus3dB, kMinus3dB, 1.0f, kMinus6dB, kMinus3dB, kMinus3dB},
    };
  }

  // Generic fallback: diagonal 1:1 mapping up to min(dst, src)
  std::vector<std::vector<float>> matrix(
      dst_channels, std::vector<float>(src_channels, 0.0f));
  for (size_t i = 0; i < std::min(dst_channels, src_channels); ++i) {
    matrix[i][i] = 1.0f;
  }
  return matrix;
}

}  // namespace

std::vector<std::vector<float>> BuildChannelMixingMatrix(
    uint8_t dst_channels, uint8_t src_channels, float volume, float fade,
    float balance) {
  // Compute acoustic cabin attenuation for the 6 standard speaker positions
  const float front_gain = (fade >= 0.0f) ? 1.0f : (1.0f + fade);
  const float rear_gain = (fade <= 0.0f) ? 1.0f : (1.0f - fade);
  const float left_gain = (balance <= 0.0f) ? 1.0f : (1.0f - balance);
  const float right_gain = (balance >= 0.0f) ? 1.0f : (1.0f + balance);

  const std::array<float, 6> spatial_gains = {
      volume * (front_gain * left_gain),   // 0: Front-Left (FL)
      volume * (front_gain * right_gain),  // 1: Front-Right (FR)
      volume * front_gain,                 // 2: Front-Center (FC)
      volume,                              // 3: Subwoofer (LFE)
      volume * (rear_gain * left_gain),    // 4: Rear-Left (RL)
      volume * (rear_gain * right_gain),   // 5: Rear-Right (RR)
  };

  // Get the base ITU-R topology matrix B
  auto channels_map = GetItuDownmixMatrix(dst_channels, src_channels);

  // Apply per-channel spatial gains to each column
  for (size_t i = 0; i < dst_channels; ++i) {
    for (size_t j = 0; j < src_channels; ++j) {
      const float gain = (j < spatial_gains.size()) ? spatial_gains[j] : volume;
      channels_map[i][j] *= gain;
    }
  }

  return channels_map;
}

}  // namespace cuttlefish
