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
#pragma once

#include <stdint.h>

#include <vector>

#include "host/libs/audio_connector/shm_layout.h"

namespace cuttlefish {

class AudioCommand {
 public:
  virtual ~AudioCommand();

  AudioCommandType type() const { return type_; }
  AudioStatus status() const { return status_; }

 protected:
  AudioCommand(AudioCommandType type) : type_(type) {}

  void MarkReplied(AudioStatus status) { status_ = status; }

 private:
  AudioStatus status_ = AudioStatus::NOT_SET;
  const AudioCommandType type_;
};

template <typename R>
class InfoCommand : public AudioCommand {
 public:
  InfoCommand(AudioCommandType type, uint32_t start_id, size_t count, R* reply)
      : AudioCommand(type),
        start_id_(start_id),
        count_(count),
        info_reply_(reply) {}

  uint32_t start_id() const { return start_id_; }
  uint32_t count() const { return count_; }

 protected:
  R* info_reply() { return info_reply_; }

 private:
  const uint32_t start_id_;
  const size_t count_;
  R* info_reply_;
};

class ChmapInfoCommand : public InfoCommand<virtio_snd_chmap_info> {
 public:
  ChmapInfoCommand(uint32_t start_id, size_t count,
                   virtio_snd_chmap_info* chmap_info);

  void Reply(AudioStatus status,
             const std::vector<virtio_snd_chmap_info>& reply);
};

class JackInfoCommand : public InfoCommand<virtio_snd_jack_info> {
 public:
  JackInfoCommand(uint32_t start_id, size_t count,
                   virtio_snd_jack_info* jack_info);

  void Reply(AudioStatus status,
             const std::vector<virtio_snd_jack_info>& reply);
};

class StreamInfoCommand : public InfoCommand<virtio_snd_pcm_info> {
 public:
  StreamInfoCommand(uint32_t start_id, size_t count,
                    virtio_snd_pcm_info* pcm_info);

  void Reply(AudioStatus status, const std::vector<virtio_snd_pcm_info>& reply);
};

// Serves the START, STOP, PREPARE and RELEASE commands. It's also the
// superclass of the class handling SET_PARAMS
struct StreamControlCommand : public AudioCommand {
 public:
  StreamControlCommand(AudioCommandType type, uint32_t stream_id);

  uint32_t stream_id() const { return stream_id_; }

  void Reply(AudioStatus status);

 private:
  const uint32_t stream_id_;
};

struct StreamSetParamsCommand : public StreamControlCommand {
 public:
  StreamSetParamsCommand(uint32_t stream_id, uint32_t buffer_bytes,
                         uint32_t period_bytes, uint32_t features,
                         uint8_t channels, uint8_t format, uint8_t rate);

  uint32_t buffer_bytes() const { return buffer_bytes_; }
  uint32_t period_bytes() const { return period_bytes_; }
  uint32_t features() const { return features_; }
  uint8_t channels() const { return channels_; }
  uint8_t format() const { return format_; }
  uint8_t rate() const { return rate_; }

 private:
  const uint32_t buffer_bytes_;
  const uint32_t period_bytes_;
  const uint32_t features_;
  const uint8_t channels_;
  const uint8_t format_;
  const uint8_t rate_;
};

}  // namespace cuttlefish
