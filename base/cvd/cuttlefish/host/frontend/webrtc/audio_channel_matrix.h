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

#include <cstdint>
#include <vector>

namespace cuttlefish {

/**
 * Builds the channel mixing matrix of dimension [dst_channels][src_channels].
 *
 * Implements standard ITU-R BS.775 downmixing (e.g. 5.1 -> Stereo, 5.1 -> Mono)
 * combined with per-stream spatial cabin attenuation (fade and balance).
 *
 * @param dst_channels Destination speaker channels (e.g. 2 for Stereo host sink)
 * @param src_channels Source stream channels (e.g. 6 for 5.1 Surround, 2 for Stereo)
 * @param volume Master stream volume [0.0 - 1.0]
 * @param fade Front/Rear cabin fader [-1.0 (Rear) to 1.0 (Front)]
 * @param balance Left/Right cabin balance [-1.0 (Left) to 1.0 (Right)]
 * @param is_ducked Whether the stream is ducked (-14 dB attenuation)
 */
std::vector<std::vector<float>> BuildChannelMixingMatrix(
    uint8_t dst_channels, uint8_t src_channels, float volume, float fade,
    float balance, bool is_ducked = false);

}  // namespace cuttlefish
