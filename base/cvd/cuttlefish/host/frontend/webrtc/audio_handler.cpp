/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "cuttlefish/host/frontend/webrtc/audio_handler.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>

#include <android-base/logging.h>
#include <rtc_base/time_utils.h>

#include "cuttlefish/host/frontend/webrtc/audio_mixer.h"

namespace cuttlefish {
namespace {

const virtio_snd_jack_info JACKS[] = {};
constexpr uint32_t NUM_JACKS = sizeof(JACKS) / sizeof(JACKS[0]);

inline AudioStreamDirection ToVirtioDirection(
    AudioStreamSettings::Direction direction) {
  static const std::unordered_map<AudioStreamSettings::Direction,
                                  AudioStreamDirection>
      kDirectionMap = {
          {AudioStreamSettings::Direction::Capture,
           AudioStreamDirection::VIRTIO_SND_D_INPUT},
          {AudioStreamSettings::Direction::Playback,
           AudioStreamDirection::VIRTIO_SND_D_OUTPUT},
      };
  return kDirectionMap.at(direction);
}

virtio_snd_chmap_info GetVirtioSndChmapInfo(
    const AudioStreamSettings& settings) {
  const static std::unordered_map<AudioChannelsLayout, std::vector<uint8_t>>
      kChannelPositions = {
          {AudioChannelsLayout::Mono, {AudioChannelMap::VIRTIO_SND_CHMAP_MONO}},
          {AudioChannelsLayout::Stereo,
           {AudioChannelMap::VIRTIO_SND_CHMAP_FL,
            AudioChannelMap::VIRTIO_SND_CHMAP_FR}},
          {AudioChannelsLayout::Surround51,
           {AudioChannelMap::VIRTIO_SND_CHMAP_FL,
            AudioChannelMap::VIRTIO_SND_CHMAP_FR,
            AudioChannelMap::VIRTIO_SND_CHMAP_FC,
            AudioChannelMap::VIRTIO_SND_CHMAP_LFE,
            AudioChannelMap::VIRTIO_SND_CHMAP_RL,
            AudioChannelMap::VIRTIO_SND_CHMAP_RR}},
      };
  CHECK(kChannelPositions.count(settings.channels_layout));
  virtio_snd_chmap_info info = {
      .hdr =
          {
              .hda_fn_nid = Le32(settings.id),
          },
      .direction = static_cast<uint8_t>(ToVirtioDirection(settings.direction)),
      .channels = GetChannelsCount(settings.channels_layout),
  };
  const auto& pos = kChannelPositions.at(settings.channels_layout);
  std::copy(pos.cbegin(), pos.cend(), info.positions);
  return info;
}

inline constexpr const char* GetDirectionString(
    AudioStreamSettings::Direction direction) {
  switch (direction) {
    case AudioStreamSettings::Direction::Capture:
      return "Capture";
    case AudioStreamSettings::Direction::Playback:
      return "Playback";
  }
}

virtio_snd_ctl_info GetVirtioCtlInfoVolume(
    const AudioStreamSettings::VolumeControl& settings,
    AudioStreamSettings::Direction stream_direction, uint32_t card_id,
    uint32_t device_id, uint32_t ctl_id) {
  virtio_snd_ctl_info info = {
      .hdr = {.hda_fn_nid = Le32(ctl_id)},
      .role = Le32(AudioControlRole::VIRTIO_SND_CTL_ROLE_VOLUME),
      .type = Le32(AudioControlType::VIRTIO_SND_CTL_TYPE_INTEGER),
      .access = Le32((1 << AudioControlAccess::VIRTIO_SND_CTL_ACCESS_READ) |
                     (1 << AudioControlAccess::VIRTIO_SND_CTL_ACCESS_WRITE)),
      .count = Le32(1),
      .index = Le32(0),
      .name = {},
      .value = {.integer = {
                    .min = Le32(settings.min),
                    .max = Le32(settings.max),
                    .step = Le32(settings.step),
                }}};
  std::format_to_n(info.name, sizeof(info.name) - 1,
                   "Master {} Volume (C{}D{})",
                   GetDirectionString(stream_direction), card_id, device_id);
  return info;
}

virtio_snd_ctl_info GetVirtioCtlInfoMute(
    AudioStreamSettings::Direction stream_direction, uint32_t card_id,
    uint32_t device_id, uint32_t ctl_id) {
  virtio_snd_ctl_info info = {
      .hdr = {.hda_fn_nid = Le32(ctl_id)},
      .role = Le32(AudioControlRole::VIRTIO_SND_CTL_ROLE_MUTE),
      .type = Le32(AudioControlType::VIRTIO_SND_CTL_TYPE_BOOLEAN),
      .access = Le32((1 << AudioControlAccess::VIRTIO_SND_CTL_ACCESS_READ) |
                     (1 << AudioControlAccess::VIRTIO_SND_CTL_ACCESS_WRITE)),
      .count = Le32(1),
      .index = Le32(0),
      .name = {},
      .value = {}  // Ignored when VIRTIO_SND_CTL_TYPE_BOOLEAN
  };
  std::format_to_n(info.name, sizeof(info.name) - 1, "Master {} Mute (C{}D{})",
                   GetDirectionString(stream_direction), card_id, device_id);
  return info;
}

virtio_snd_pcm_info GetVirtioSndPcmInfo(const AudioStreamSettings& settings) {
  return {
      .hdr =
          {
              .hda_fn_nid = Le32(settings.id),
          },
      .features = Le32(0),
      // webrtc's api is quite primitive and doesn't allow for many different
      // formats: It only takes the bits_per_sample as a parameter and assumes
      // the underlying format to be one of the following:
      .formats = Le64((((uint64_t)1)
                       << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S8) |
                      (((uint64_t)1)
                       << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S16) |
                      (((uint64_t)1)
                       << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S24) |
                      (((uint64_t)1)
                       << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S32)),
      .rates = Le64((((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_5512) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_8000) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_11025) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_16000) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_22050) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_32000) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_44100) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_48000) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_64000) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_88200) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_96000) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_176400) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_192000) |
                    (((uint64_t)1)
                     << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_384000)),
      .direction = static_cast<uint8_t>(ToVirtioDirection(settings.direction)),
      .channels_min = 1,
      .channels_max = GetChannelsCount(settings.channels_layout),
  };
}

int BitsPerSample(uint8_t virtio_format) {
  switch (virtio_format) {
    /* analog formats (width / physical width) */
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_IMA_ADPCM:
      /*  4 /  4 bits */
      return 4;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_MU_LAW:
      /*  8 /  8 bits */
      return 8;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_A_LAW:
      /*  8 /  8 bits */
      return 8;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S8:
      /*  8 /  8 bits */
      return 8;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U8:
      /*  8 /  8 bits */
      return 8;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S16:
      /* 16 / 16 bits */
      return 16;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U16:
      /* 16 / 16 bits */
      return 16;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S18_3:
      /* 18 / 24 bits */
      return 24;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U18_3:
      /* 18 / 24 bits */
      return 24;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S20_3:
      /* 20 / 24 bits */
      return 24;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U20_3:
      /* 20 / 24 bits */
      return 24;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S24_3:
      /* 24 / 24 bits */
      return 24;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U24_3:
      /* 24 / 24 bits */
      return 24;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S20:
      /* 20 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U20:
      /* 20 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S24:
      /* 24 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U24:
      /* 24 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S32:
      /* 32 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U32:
      /* 32 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_FLOAT:
      /* 32 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_FLOAT64:
      /* 64 / 64 bits */
      return 64;
    /* digital formats (width / physical width) */
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_DSD_U8:
      /*  8 /  8 bits */
      return 8;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_DSD_U16:
      /* 16 / 16 bits */
      return 16;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_DSD_U32:
      /* 32 / 32 bits */
      return 32;
    case (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME:
      /* 32 / 32 bits */
      return 32;
    default:
      LOG(ERROR) << "Unknown virtio-snd audio format: " << virtio_format;
      return -1;
  }
}

int SampleRate(uint8_t virtio_rate) {
  switch (virtio_rate) {
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_5512:
      return 5512;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_8000:
      return 8000;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_11025:
      return 11025;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_16000:
      return 16000;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_22050:
      return 22050;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_32000:
      return 32000;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_44100:
      return 44100;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_48000:
      return 48000;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_64000:
      return 64000;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_88200:
      return 88200;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_96000:
      return 96000;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_176400:
      return 176400;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_192000:
      return 192000;
    case (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_384000:
      return 384000;
    default:
      LOG(ERROR) << "Unknown virtio-snd sample rate: " << virtio_rate;
      return -1;
  }
}

}  // namespace

AudioHandler::AudioHandler(
    std::unique_ptr<AudioServer> audio_server,
    std::shared_ptr<webrtc_streaming::AudioSink> audio_sink,
    std::shared_ptr<webrtc_streaming::AudioSource> audio_source,
    const std::vector<AudioStreamSettings>& stream_settings,
    const AudioMixerSettings& mixer_settings)
    : audio_server_(std::move(audio_server)),
      audio_source_(audio_source),
      stream_descs_(stream_settings.size()),
      chmaps_(stream_descs_.size()),
      audio_mixer_(
          std::make_unique<AudioMixer>(std::move(audio_sink), mixer_settings)) {
  streams_ = std::vector<virtio_snd_pcm_info>(stream_descs_.size());
  const auto input_streams_count = static_cast<size_t>(std::count_if(
      stream_settings.cbegin(), stream_settings.cend(),
      [](const AudioStreamSettings& settings) {
        return settings.direction == AudioStreamSettings::Direction::Capture;
      }));
  for (const auto& settings : stream_settings) {
    const auto stream_id =
        settings.id +
        (settings.direction == AudioStreamSettings::Direction::Playback
             ? input_streams_count
             : 0);
    streams_[stream_id] = GetVirtioSndPcmInfo(settings);
    chmaps_[stream_id] = GetVirtioSndChmapInfo(settings);

    constexpr uint32_t kCardId = 0;  // As of now only one card is supported
    if (settings.has_mute_control) {
      controls_.push_back(GetVirtioCtlInfoMute(settings.direction, kCardId,
                                               settings.id, controls_.size()));
      controls_to_streams_map_.push_back(
          ControlDesc{.type = ControlDesc::Type::Mute, .stream_id = stream_id});
    }
    if (settings.master_volume_control.has_value()) {
      const auto& control = settings.master_volume_control.value();
      CHECK(control.max > control.min)
          << "Volume control 'min' value must be lower than 'max'.";
      CHECK((control.max - control.min) % control.step == 0)
          << "Volume control 'step' value must divide the total volume range "
             "evenly.";
      controls_.push_back(GetVirtioCtlInfoVolume(
          control, settings.direction, kCardId, settings.id, controls_.size()));
      controls_to_streams_map_.push_back(ControlDesc{
          .type = ControlDesc::Type::Volume, .stream_id = stream_id});
      stream_descs_[stream_id].volume = {
          .min = control.min,
          .max = control.max,
          .current = control.max,
      };
    }
  }
}

AudioHandler::~AudioHandler() { audio_mixer_->Stop(); }

void AudioHandler::Start() {
  server_thread_ = std::thread([this]() { Loop(); });
  audio_mixer_->Start();
}

[[noreturn]] void AudioHandler::Loop() {
  for (;;) {
    auto audio_client = audio_server_->AcceptClient(
        streams_.size(), NUM_JACKS, chmaps_.size(), controls_.size(),
        262144 /* tx_shm_len */, 262144 /* rx_shm_len */);
    CHECK(audio_client) << "Failed to create audio client connection instance";

    std::thread playback_thread([this, &audio_client]() {
      while (audio_client->ReceivePlayback(*this)) {
      }
    });
    std::thread capture_thread([this, &audio_client]() {
      while (audio_client->ReceiveCapture(*this)) {
      }
    });
    // Wait for the client to do something
    while (audio_client->ReceiveCommands(*this)) {
    }
    playback_thread.join();
    capture_thread.join();
  }
}

void AudioHandler::StreamsInfo(StreamInfoCommand& cmd) {
  if (cmd.start_id() >= streams_.size() ||
      cmd.start_id() + cmd.count() > streams_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG, {});
    return;
  }
  std::vector<virtio_snd_pcm_info> stream_info(
      &streams_[cmd.start_id()], &streams_[0] + cmd.start_id() + cmd.count());
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK, stream_info);
}

void AudioHandler::SetStreamParameters(StreamSetParamsCommand& cmd) {
  if (cmd.stream_id() >= streams_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  const auto& stream_info = streams_[cmd.stream_id()];
  auto bits_per_sample = BitsPerSample(cmd.format());
  auto sample_rate = SampleRate(cmd.rate());
  auto channels = cmd.channels();
  if (bits_per_sample <= 0 || sample_rate <= 0 ||
      channels < stream_info.channels_min ||
      channels > stream_info.channels_max) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(stream_descs_[cmd.stream_id()].mtx);
    stream_descs_[cmd.stream_id()].bits_per_sample = bits_per_sample;
    stream_descs_[cmd.stream_id()].sample_rate = sample_rate;
    stream_descs_[cmd.stream_id()].channels = channels;
  }
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::PrepareStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= streams_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::ReleaseStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= streams_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::StartStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= streams_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  auto& stream_desc = stream_descs_[cmd.stream_id()];
  stream_desc.active = true;
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::StopStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= streams_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  auto& stream_desc = stream_descs_[cmd.stream_id()];
  stream_desc.active = false;
  audio_mixer_->OnStreamStopped(cmd.stream_id());
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::ChmapsInfo(ChmapInfoCommand& cmd) {
  if (cmd.start_id() >= chmaps_.size() ||
      cmd.start_id() + cmd.count() > chmaps_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG, {});
    return;
  }
  std::vector<virtio_snd_chmap_info> chmap_info(
      &chmaps_[cmd.start_id()], &chmaps_[cmd.start_id()] + cmd.count());
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK, chmap_info);
}

void AudioHandler::JacksInfo(JackInfoCommand& cmd) {
  if (cmd.start_id() >= NUM_JACKS || cmd.start_id() + cmd.count() > NUM_JACKS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG, {});
    return;
  }
  std::vector<virtio_snd_jack_info> jack_info(
      &JACKS[cmd.start_id()], &JACKS[cmd.start_id()] + cmd.count());
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK, jack_info);
}

void AudioHandler::ControlsInfo(ControlInfoCommand& cmd) {
  LOG(DEBUG) << "AudioHandler::ControlsInfo: start_id=" << cmd.start_id()
             << ", count=" << cmd.count();

  if (cmd.start_id() + cmd.count() > controls_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG, {});
    return;
  }

  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK,
            {controls_.cbegin() + cmd.start_id(), cmd.count()});
}

void AudioHandler::OnControlCommand(ControlCommand& cmd) {
  const auto id = cmd.control_id();
  if (id >= controls_.size()) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }

  LOG(DEBUG) << "AudioHandler::OnControlCommand: control_id=" << id;

  const auto stream_id = controls_to_streams_map_[id].stream_id;
  auto& stream = stream_descs_[stream_id];

  const auto control_mute = [stream_id, &stream, &cmd]() -> AudioStatus {
    std::lock_guard<std::mutex> lock(stream.mtx);

    if (cmd.type() == AudioCommandType::VIRTIO_SND_R_CTL_READ) {
      auto& val = cmd.value()->value.integer;
      val.value[0] = Le32(stream.muted ? 1 : 0);
      return AudioStatus::VIRTIO_SND_S_OK;
    }

    if (cmd.type() == AudioCommandType::VIRTIO_SND_R_CTL_WRITE) {
      const auto val = cmd.value()->value.integer.value[0].as_uint32_t();
      // For VIRTIO_SND_CTL_TYPE_BOOLEAN, the guest driver (virtio_snd)
      // inherently knows that the control acts as a toggle. It implicitly
      // enforces a minimum of 0 and a maximum of 1.
      if (val > 1) {
        LOG(ERROR) << "Wrongs boolean value for control " << cmd.control_id()
                   << " provided: " << val;
        return AudioStatus::VIRTIO_SND_S_BAD_MSG;
      }
      LOG(DEBUG) << "Setting mute for stream " << stream_id << " to " << val;
      stream.muted = val == 1;
      return AudioStatus::VIRTIO_SND_S_OK;
    }

    return AudioStatus::VIRTIO_SND_S_NOT_SUPP;
  };

  const auto control_volume = [stream_id, &stream, &cmd]() -> AudioStatus {
    std::lock_guard<std::mutex> lock(stream.mtx);

    if (cmd.type() == AudioCommandType::VIRTIO_SND_R_CTL_READ) {
      auto& val = cmd.value()->value.integer;
      val.value[0] = Le32(stream.volume.current);
      return AudioStatus::VIRTIO_SND_S_OK;
    }

    if (cmd.type() == AudioCommandType::VIRTIO_SND_R_CTL_WRITE) {
      const auto val = cmd.value()->value.integer.value[0].as_uint32_t();
      if (val < stream.volume.min || val > stream.volume.max) {
        LOG(ERROR) << "Wrongs volume value for control " << cmd.control_id()
                   << " provided: " << val;
        return AudioStatus::VIRTIO_SND_S_BAD_MSG;
      }
      LOG(DEBUG) << "Setting volume for stream " << stream_id << " to " << val;
      stream.volume.current = val;
      return AudioStatus::VIRTIO_SND_S_OK;
    }

    return AudioStatus::VIRTIO_SND_S_NOT_SUPP;
  };

  auto result = AudioStatus::VIRTIO_SND_S_NOT_SUPP;
  switch (controls_to_streams_map_[id].type) {
    case ControlDesc::Type::Mute:
      result = control_mute();
      break;
    case ControlDesc::Type::Volume:
      result = control_volume();
      break;
  }
  cmd.Reply(result);
}

void AudioHandler::OnPlaybackBuffer(TxBuffer buffer) {
  const auto stream_id = buffer.stream_id();
  // Invalid or capture streams shouldn't send tx buffers
  if (stream_id >= streams_.size() || IsCapture(stream_id)) {
    LOG(ERROR) << "Invalid or capture streams have sent tx buffers";
    buffer.SendStatus(AudioStatus::VIRTIO_SND_S_BAD_MSG, 0, 0);
    return;
  }

  uint32_t sample_rate = 0;
  uint8_t channels = 0;
  uint8_t bits_per_channel = 0;
  float volume = 0;
  {
    auto& stream_desc = stream_descs_[stream_id];
    std::lock_guard<std::mutex> lock(stream_desc.mtx);

    volume = stream_desc.volume.GetCurrentVolumeLevel();

    // A buffer may be received for an inactive stream if we were slow to
    // process it and the other side stopped the stream. Quietly ignore it in
    // that case
    if (!stream_desc.active || stream_desc.muted || volume == 0) {
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
      return;
    }

    sample_rate = stream_desc.sample_rate;
    channels = stream_desc.channels;
    bits_per_channel = stream_desc.bits_per_sample;
  }
  audio_mixer_->OnPlayback(stream_id, sample_rate, channels, bits_per_channel,
                           volume, buffer.get(), buffer.len());
  buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
}

void AudioHandler::OnCaptureBuffer(RxBuffer buffer) {
  const auto rx_buffer = buffer.get();
  size_t bytes_read = 0;
  float volume = 0;
  uint32_t bytes_per_sample = 0;
  bool is_muted_by_control = false;

  auto stream_id = buffer.stream_id();
  auto& stream_desc = stream_descs_[stream_id];
  {
    std::lock_guard<std::mutex> lock(stream_desc.mtx);
    // Invalid or playback streams shouldn't send rx buffers
    if (stream_id >= streams_.size() || !IsCapture(stream_id)) {
      LOG(ERROR) << "Received capture buffers on playback stream " << stream_id;
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_BAD_MSG, 0, 0);
      return;
    }

    // A buffer may be received for an inactive stream if we were slow to
    // process it and the other side stopped the stream. Quietly ignore it in
    // that case
    if (!stream_desc.active) {
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
      return;
    }
    volume = stream_desc.volume.GetCurrentVolumeLevel();
    is_muted_by_control = stream_desc.muted;
    bytes_per_sample = stream_desc.bits_per_sample / 8;
    const auto samples_per_channel = stream_desc.sample_rate / 100;
    const auto bytes_per_request =
        samples_per_channel * bytes_per_sample * stream_desc.channels;

    // Fill remaining data from previous iteration
    auto& holding_buffer = stream_descs_[stream_id].holding_buffer;
    bytes_read = holding_buffer.size();
    std::copy(holding_buffer.cbegin(), holding_buffer.cend(), rx_buffer);
    holding_buffer.clear();

    bool muted = false;
    while (buffer.len() - bytes_read >= bytes_per_request) {
      // Skip the holding buffer in as many reads as possible to avoid the extra
      // copies
      const auto write_pos = rx_buffer + bytes_read;
      auto res = audio_source_->GetMoreAudioData(
          write_pos, bytes_per_sample, samples_per_channel,
          stream_desc.channels, stream_desc.sample_rate, muted);
      if (res < 0) {
        // This is likely a recoverable error, log the error but don't let the
        // VMM know about it so that it doesn't crash.
        LOG(ERROR) << "Failed to receive audio data from client";
        break;
      }
      if (muted) {
        // The source is muted, just fill the buffer with zeros and return
        memset(rx_buffer + bytes_read, 0, buffer.len() - bytes_read);
        bytes_read = buffer.len();
        break;
      }
      auto bytes_received = res * bytes_per_sample * stream_desc.channels;
      bytes_read += bytes_received;
    }
    if (bytes_read < buffer.len()) {
      // There is some buffer left to fill, but it's less than 10ms, read into
      // holding buffer to ensure the remainder is kept around for future reads
      holding_buffer.resize(bytes_per_request);
      auto res = audio_source_->GetMoreAudioData(
          holding_buffer.data(), bytes_per_sample, samples_per_channel,
          stream_desc.channels, stream_desc.sample_rate, muted);
      if (res < 0) {
        // This is likely a recoverable error, log the error but don't let the
        // VMM know about it so that it doesn't crash.
        LOG(ERROR) << "Failed to receive audio data from client";
      } else if (muted) {
        // The source is muted, just fill the buffer with zeros and return
        memset(rx_buffer + bytes_read, 0, buffer.len() - bytes_read);
      } else {
        const auto bytes_to_read = buffer.len() - bytes_read;
        std::copy(holding_buffer.data(), holding_buffer.data() + bytes_to_read,
                  rx_buffer + bytes_read);
        bytes_read += bytes_to_read;

        const auto new_size = holding_buffer.size() - bytes_to_read;
        std::memmove(holding_buffer.data(),
                     holding_buffer.data() + bytes_to_read, new_size);
        holding_buffer.resize(new_size);
        // If the entire buffer is not full by now there is a bug above
        // somewhere
        CHECK(bytes_read == buffer.len()) << "Failed to read entire buffer";
      }
    }
  }

  if (is_muted_by_control) {
    memset(rx_buffer, 0, bytes_read);
  } else if (volume < 1.) {
    static const auto apply_volume = [](auto* data, size_t size_bytes,
                                        float volume) {
      for (auto& val : std::span{data, size_bytes / sizeof(*data)}) {
        val *= volume;
      }
    };
    switch (bytes_per_sample) {
      case 1:
        apply_volume(rx_buffer, bytes_read, volume);
        break;
      case 2:
        apply_volume(reinterpret_cast<uint16_t*>(rx_buffer), bytes_read,
                     volume);
        break;
      case 4:
        apply_volume(reinterpret_cast<uint32_t*>(rx_buffer), bytes_read,
                     volume);
        break;
    }
  }

  buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
}

bool AudioHandler::IsCapture(uint32_t stream_id) const {
  CHECK(stream_id < streams_.size()) << "Invalid stream id: " << stream_id;
  return streams_[stream_id].direction ==
         (uint8_t)AudioStreamDirection::VIRTIO_SND_D_INPUT;
}

}  // namespace cuttlefish
