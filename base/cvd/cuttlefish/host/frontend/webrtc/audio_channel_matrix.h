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
 * Builds the matrix used to mix a stream's source channels into a sink's
 * destination channels.
 *
 * Each source channel is mapped to the destination channel of the same index
 * and scaled by `volume`. Channels present on only one side are dropped.
 *
 * @param dst_channels Destination sink channels (e.g. 2 for a stereo sink)
 * @param src_channels Source stream channels (e.g. 6 for 5.1 surround)
 * @param volume Master stream volume [0.0 - 1.0]
 * @return Mixing coefficients, indexed [destination channel][source channel]
 */
std::vector<std::vector<float>> BuildChannelMixingMatrix(uint8_t dst_channels,
                                                         uint8_t src_channels,
                                                         float volume);

}  // namespace cuttlefish
