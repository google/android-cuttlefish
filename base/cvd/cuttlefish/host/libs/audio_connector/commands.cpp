//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/libs/audio_connector/commands.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <android-base/logging.h>

#include "host/libs/audio_connector/shm_layout.h"

namespace cuttlefish {

AudioCommand::~AudioCommand() {
  CHECK(status_ != AudioStatus::NOT_SET)
      << "A command of type " << static_cast<uint32_t>(type())
      << " went out of scope without reply";
}

JackInfoCommand::JackInfoCommand(uint32_t start_id, size_t count,
                                 virtio_snd_jack_info* jack_info)
    : InfoCommand(AudioCommandType::VIRTIO_SND_R_CHMAP_INFO, start_id, count,
                  jack_info) {}

void JackInfoCommand::Reply(AudioStatus status,
                            const std::vector<virtio_snd_jack_info>& reply) {
  MarkReplied(status);
  if (status != AudioStatus::VIRTIO_SND_S_OK) {
    return;
  }
  CHECK(reply.size() == count())
      << "Returned unmatching info count: " << reply.size() << " vs "
      << count();
  for (int i = 0; i < reply.size(); ++i) {
    info_reply()[i] = reply[i];
  }
}

ChmapInfoCommand::ChmapInfoCommand(uint32_t start_id, size_t count,
                                   virtio_snd_chmap_info* chmap_info)
    : InfoCommand(AudioCommandType::VIRTIO_SND_R_CHMAP_INFO, start_id, count,
                  chmap_info) {}

void ChmapInfoCommand::Reply(AudioStatus status,
                             const std::vector<virtio_snd_chmap_info>& reply) {
  MarkReplied(status);
  if (status != AudioStatus::VIRTIO_SND_S_OK) {
    return;
  }
  CHECK(reply.size() == count())
      << "Returned unmatching info count: " << reply.size() << " vs "
      << count();
  for (int i = 0; i < reply.size(); ++i) {
    info_reply()[i].hdr.hda_fn_nid = Le32(reply[i].hdr.hda_fn_nid);
    info_reply()[i].direction = reply[i].direction;
    auto channels = std::min(VIRTIO_SND_CHMAP_MAX_SIZE, reply[i].channels);
    info_reply()[i].channels = channels;
    for (int j = 0; j < channels; ++j) {
	    info_reply()[i].positions[j] = reply[i].positions[j];
    }
  }
}

StreamInfoCommand::StreamInfoCommand(uint32_t start_id, size_t count,
                                     virtio_snd_pcm_info* pcm_info)
    : InfoCommand(AudioCommandType::VIRTIO_SND_R_PCM_INFO, start_id, count,
                  pcm_info) {}

void StreamInfoCommand::Reply(AudioStatus status,
                              const std::vector<virtio_snd_pcm_info>& reply) {
  MarkReplied(status);
  if (status != AudioStatus::VIRTIO_SND_S_OK) {
    return;
  }
  CHECK(reply.size() == count())
      << "Returned unmatching info count: " << reply.size() << " vs "
      << count();
  for (int i = 0; i < reply.size(); ++i) {
    info_reply()[i].hdr.hda_fn_nid = Le32(reply[i].hdr.hda_fn_nid);
    info_reply()[i].features = Le32(reply[i].features);
    info_reply()[i].formats = Le64(reply[i].formats);
    info_reply()[i].rates = Le64(reply[i].rates);
    info_reply()[i].direction = reply[i].direction;
    info_reply()[i].channels_min = reply[i].channels_min;
    info_reply()[i].channels_max = reply[i].channels_max;
    // pcm_info[i].padding is supposed to be all zeros in virtio-snd but here we
    // can just ignore it.
  }
}

StreamControlCommand::StreamControlCommand(AudioCommandType type,
                                           uint32_t stream_id)
    : AudioCommand(type), stream_id_(stream_id) {}

void StreamControlCommand::Reply(AudioStatus status) {
  // These commands don't expect a reply, this method just forces
  // acknowledgement of the command.
  MarkReplied(status);
}

StreamSetParamsCommand::StreamSetParamsCommand(
    uint32_t stream_id, uint32_t buffer_bytes, uint32_t period_bytes,
    uint32_t features, uint8_t channels, uint8_t format, uint8_t rate)
    : StreamControlCommand(AudioCommandType::VIRTIO_SND_R_PCM_SET_PARAMS,
                           stream_id),
      buffer_bytes_(buffer_bytes),
      period_bytes_(period_bytes),
      features_(features),
      channels_(channels),
      format_(format),
      rate_(rate) {}

}  // namespace cuttlefish
