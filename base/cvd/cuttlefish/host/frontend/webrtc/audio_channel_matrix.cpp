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

}  // namespace

std::vector<std::vector<float>> BuildChannelMixingMatrix(
    uint8_t dst_channels, uint8_t src_channels, float volume, float fade,
    float balance) {
  constexpr uint8_t kMono = GetChannelsCount(AudioChannelsLayout::Mono);        // 1
  constexpr uint8_t kStereo = GetChannelsCount(AudioChannelsLayout::Stereo);    // 2
  constexpr uint8_t kSurround51 =
      GetChannelsCount(AudioChannelsLayout::Surround51);                        // 6

  // Compute acoustic cabin attenuation for the 4 quadrants
  const float front_gain = (fade >= 0.0f) ? 1.0f : (1.0f + fade);
  const float rear_gain = (fade <= 0.0f) ? 1.0f : (1.0f - fade);
  const float left_gain = (balance <= 0.0f) ? 1.0f : (1.0f - balance);
  const float right_gain = (balance >= 0.0f) ? 1.0f : (1.0f + balance);

  const float fl_gain = volume * (front_gain * left_gain);
  const float fr_gain = volume * (front_gain * right_gain);
  const float fc_gain = volume * front_gain;
  const float rl_gain = volume * (rear_gain * left_gain);
  const float rr_gain = volume * (rear_gain * right_gain);

  // Case 1: Stereo Destination Output (Laptop Speakers / WebRTC sink)
  if (dst_channels == kStereo) {
    if (src_channels == kSurround51) {
      // 5.1 Surround -> Stereo (ITU-R BS.775 with left/right balance & front/rear fade)
      return {
          {fl_gain, 0.0f, kMinus3dB * fc_gain * left_gain,
           kMinus6dB * volume * left_gain, kMinus3dB * rl_gain, 0.0f},
          {0.0f, fr_gain, kMinus3dB * fc_gain * right_gain,
           kMinus6dB * volume * right_gain, 0.0f, kMinus3dB * rr_gain},
      };
    }
    if (src_channels == kStereo) {
      // Stereo -> Stereo (Direct with left/right balance & front/rear fade)
      return {
          {fl_gain, 0.0f},
          {0.0f, fr_gain},
      };
    }
    if (src_channels == kMono) {
      // Mono -> Stereo (Center mono panned by balance)
      return {
          {fl_gain},
          {fr_gain},
      };
    }
  }

  // Case 2: Mono Destination Output
  if (dst_channels == kMono) {
    if (src_channels == kSurround51) {
      return {
          {kMinus3dB * fl_gain, kMinus3dB * fr_gain, fc_gain,
           kMinus6dB * volume, kMinus3dB * rl_gain, kMinus3dB * rr_gain},
      };
    }
    if (src_channels == kStereo) {
      return {
          {kMinus6dB * fl_gain, kMinus6dB * fr_gain},
      };
    }
    if (src_channels == kMono) {
      return {
          {fl_gain},
      };
    }
  }

  // Fallback: generic diagonal matrix
  std::vector<std::vector<float>> matrix(
      dst_channels, std::vector<float>(src_channels, 0.0f));
  const std::array<float, 6> spatial_gains = {fl_gain, fr_gain, fc_gain,
                                              volume,  rl_gain, rr_gain};
  for (size_t i = 0; i < std::min(dst_channels, src_channels); ++i) {
    matrix[i][i] = (i < spatial_gains.size()) ? spatial_gains[i] : volume;
  }
  return matrix;
}

}  // namespace cuttlefish
