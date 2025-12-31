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
#include <array>
#include <chrono>
#include <cstring>

#include <rtc_base/time_utils.h>
#include "absl/log/check.h"
#include "absl/log/log.h"

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
          {AudioChannelsLayout::Mono,
           {AudioChannelMap::VIRTIO_SND_CHMAP_MONO}},
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
        streams_.size(), NUM_JACKS, chmaps_.size(), 262144 /* tx_shm_len */,
        262144 /* rx_shm_len */);
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
  {
    auto& stream_desc = stream_descs_[stream_id];
    std::lock_guard<std::mutex> lock(stream_desc.mtx);

    // A buffer may be received for an inactive stream if we were slow to
    // process it and the other side stopped the stream. Quietly ignore it in
    // that case
    if (!stream_desc.active) {
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
      return;
    }

    sample_rate = stream_desc.sample_rate;
    channels = stream_desc.channels;
    bits_per_channel = stream_desc.bits_per_sample;
  }
  audio_mixer_->OnPlayback(stream_id, sample_rate, channels, bits_per_channel,
                           buffer.get(), buffer.len());
  buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
}

void AudioHandler::OnCaptureBuffer(RxBuffer buffer) {
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
    const auto bytes_per_sample = stream_desc.bits_per_sample / 8;
    const auto samples_per_channel = stream_desc.sample_rate / 100;
    const auto bytes_per_request =
        samples_per_channel * bytes_per_sample * stream_desc.channels;
    auto& holding_buffer = stream_descs_[stream_id].holding_buffer;
    size_t bytes_read = 0;
    const auto rx_buffer = buffer.get();
    if (!holding_buffer.empty()) {
      // Fill remaining data from previous iteration
      bytes_read = holding_buffer.size();
      std::copy(holding_buffer.cbegin(), holding_buffer.cend(), rx_buffer);
      holding_buffer.clear();
    }
    bool muted = false;
    while (buffer.len() - bytes_read >= bytes_per_request) {
      // Skip the holding buffer in as many reads as possible to avoid the extra
      // copies
      auto write_pos = rx_buffer + bytes_read;
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
  buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
}

bool AudioHandler::IsCapture(uint32_t stream_id) const {
  CHECK(stream_id < streams_.size()) << "Invalid stream id: " << stream_id;
  return streams_[stream_id].direction ==
         (uint8_t)AudioStreamDirection::VIRTIO_SND_D_INPUT;
}

}  // namespace cuttlefish
