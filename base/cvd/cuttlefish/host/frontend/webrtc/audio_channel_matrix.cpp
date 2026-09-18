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

namespace cuttlefish {

std::vector<std::vector<float>> BuildChannelMixingMatrix(uint8_t dst_channels,
                                                         uint8_t src_channels,
                                                         float volume) {
  std::vector<std::vector<float>> matrix(
      dst_channels, std::vector<float>(src_channels, 0.0f));

  // As of now we only use direct channel mapping
  const size_t mapped_channels = std::min(dst_channels, src_channels);
  for (size_t i = 0; i < mapped_channels; ++i) {
    matrix[i][i] = volume;
  }

  return matrix;
}

}  // namespace cuttlefish
