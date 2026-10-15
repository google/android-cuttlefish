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

#include "cuttlefish/host/libs/audio/channel_layout.h"

#include "absl/log/log.h"

#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"

namespace cuttlefish {

AudioChannelsLayout ConvertChannelLayout(
    ::cuttlefish::config::Audio_ChannelLayout layout) {
  using ChannelLayout = ::cuttlefish::config::Audio_ChannelLayout;

  switch (layout) {
    case ChannelLayout::Audio_ChannelLayout_MONO:
      return AudioChannelsLayout::Mono;
    case ChannelLayout::Audio_ChannelLayout_STEREO:
      return AudioChannelsLayout::Stereo;
    case ChannelLayout::Audio_ChannelLayout_SURROUND51:
      return AudioChannelsLayout::Surround51;
    default:
      VLOG(0) << "Unsupported channel layout: " << layout;
  }

  return AudioChannelsLayout::Stereo;
}

}  // namespace cuttlefish
