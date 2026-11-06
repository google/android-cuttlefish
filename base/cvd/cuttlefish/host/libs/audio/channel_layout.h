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

#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"

namespace cuttlefish {

enum class AudioChannelsLayout {
  Mono,
  Stereo,
  Surround51,
};

static constexpr inline uint8_t GetChannelsCount(AudioChannelsLayout layout) {
  switch (layout) {
    case AudioChannelsLayout::Mono:
      return 1;
    case AudioChannelsLayout::Stereo:
      return 2;
    case AudioChannelsLayout::Surround51:
      return 6;
  }
}

// Converts a channel layout from the guest audio config. Layouts without a
// match, including UNKNOWN (unset), become Stereo.
AudioChannelsLayout ConvertChannelLayout(
    ::cuttlefish::config::Audio_ChannelLayout layout);

}  // namespace cuttlefish
