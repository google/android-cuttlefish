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
 * The matrix describes how much every source channel contributes to every
 * destination channel: dst[i] = sum(src[j] * matrix[i][j]).
 *
 * As of now only direct channel mapping is implemented: source channel `i` is
 * routed to destination channel `i` scaled by the stream volume, which matches
 * the mapping the mixer applied before this logic was extracted. Channels that
 * do not exist on both sides are dropped.
 *
 * Keeping the mixing math in a standalone, dependency free unit makes it
 * testable on its own and gives a single place to add spatial processing
 * (fade, balance, ducking, ITU-R BS.775 downmixing) later on.
 *
 * @param dst_channels Destination sink channels (e.g. 2 for a stereo sink)
 * @param src_channels Source stream channels (e.g. 6 for 5.1 surround)
 * @param volume Master stream volume [0.0 - 1.0]
 */
std::vector<std::vector<float>> BuildChannelMixingMatrix(uint8_t dst_channels,
                                                         uint8_t src_channels,
                                                         float volume);

}  // namespace cuttlefish
