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

#include "host/frontend/webrtc/audio_handler.h"

#include <algorithm>
#include <chrono>

#include <android-base/logging.h>
#include <rtc_base/time_utils.h>

namespace cuttlefish {
namespace {

const virtio_snd_jack_info JACKS[] = {};
constexpr uint32_t NUM_JACKS = sizeof(JACKS) / sizeof(JACKS[0]);

const virtio_snd_chmap_info CHMAPS[] = {{
    .hdr = { .hda_fn_nid = Le32(0), },
    .direction = (uint8_t) AudioStreamDirection::VIRTIO_SND_D_OUTPUT,
    .channels = 2,
    .positions = {
        (uint8_t) AudioChannelMap::VIRTIO_SND_CHMAP_FL,
        (uint8_t) AudioChannelMap::VIRTIO_SND_CHMAP_FR
    },
}, {
    .hdr = { .hda_fn_nid = Le32(0), },
    .direction = (uint8_t) AudioStreamDirection::VIRTIO_SND_D_INPUT,
    .channels = 2,
    .positions = {
        (uint8_t) AudioChannelMap::VIRTIO_SND_CHMAP_FL,
        (uint8_t) AudioChannelMap::VIRTIO_SND_CHMAP_FR
    },
}};
constexpr uint32_t NUM_CHMAPS = sizeof(CHMAPS) / sizeof(CHMAPS[0]);

const virtio_snd_pcm_info STREAMS[] = {{
    .hdr =
        {
            .hda_fn_nid = Le32(0),
        },
    .features = Le32(0),
    // webrtc's api is quite primitive and doesn't allow for many different
    // formats: It only takes the bits_per_sample as a parameter and assumes
    // the underlying format to be one of the following:
    .formats = Le64(
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S8) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S16) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S24) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S32)),
    .rates = Le64(
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_5512) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_8000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_11025) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_16000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_22050) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_32000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_44100) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_48000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_64000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_88200) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_96000) |
        (((uint64_t)1) << (uint8_t)
             AudioStreamRate::VIRTIO_SND_PCM_RATE_176400) |
        (((uint64_t)1) << (uint8_t)
             AudioStreamRate::VIRTIO_SND_PCM_RATE_192000) |
        (((uint64_t)1) << (uint8_t)
             AudioStreamRate::VIRTIO_SND_PCM_RATE_384000)),
    .direction = (uint8_t)AudioStreamDirection::VIRTIO_SND_D_OUTPUT,
    .channels_min = 1,
    .channels_max = 2,
}, {
    .hdr =
        {
            .hda_fn_nid = Le32(0),
        },
    .features = Le32(0),
    // webrtc's api is quite primitive and doesn't allow for many different
    // formats: It only takes the bits_per_sample as a parameter and assumes
    // the underlying format to be one of the following:
    .formats = Le64(
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S8) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S16) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S24) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_S32)),
    .rates = Le64(
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_5512) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_8000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_11025) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_16000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_22050) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_32000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_44100) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_48000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_64000) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_88200) |
        (((uint64_t)1) << (uint8_t)AudioStreamRate::VIRTIO_SND_PCM_RATE_96000) |
        (((uint64_t)1) << (uint8_t)
             AudioStreamRate::VIRTIO_SND_PCM_RATE_176400) |
        (((uint64_t)1) << (uint8_t)
             AudioStreamRate::VIRTIO_SND_PCM_RATE_192000) |
        (((uint64_t)1) << (uint8_t)
             AudioStreamRate::VIRTIO_SND_PCM_RATE_384000)),
    .direction = (uint8_t)AudioStreamDirection::VIRTIO_SND_D_INPUT,
    .channels_min = 1,
    .channels_max = 2,
}};
constexpr uint32_t NUM_STREAMS = sizeof(STREAMS) / sizeof(STREAMS[0]);

bool IsCapture(uint32_t stream_id) {
  CHECK(stream_id < NUM_STREAMS) << "Invalid stream id: " << stream_id;
  return STREAMS[stream_id].direction ==
         (uint8_t)AudioStreamDirection::VIRTIO_SND_D_INPUT;
}

class CvdAudioFrameBuffer : public webrtc_streaming::AudioFrameBuffer {
 public:
  CvdAudioFrameBuffer(const uint8_t* buffer, int bits_per_sample,
                      int sample_rate, int channels, int frames)
      : buffer_(buffer),
        bits_per_sample_(bits_per_sample),
        sample_rate_(sample_rate),
        channels_(channels),
        frames_(frames) {}

  int bits_per_sample() const override { return bits_per_sample_; }

  int sample_rate() const override { return sample_rate_; }

  int channels() const override { return channels_; }

  int frames() const override { return frames_; }

  const uint8_t* data() const override { return buffer_; }

 private:
  const uint8_t* buffer_;
  int bits_per_sample_;
  int sample_rate_;
  int channels_;
  int frames_;
};

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
    std::shared_ptr<webrtc_streaming::AudioSource> audio_source)
    : audio_sink_(audio_sink),
      audio_server_(std::move(audio_server)),
      stream_descs_(NUM_STREAMS),
      audio_source_(audio_source) {}

void AudioHandler::Start() {
  server_thread_ = std::thread([this]() { Loop(); });
}

[[noreturn]] void AudioHandler::Loop() {
  for (;;) {
    auto audio_client = audio_server_->AcceptClient(
        NUM_STREAMS, NUM_JACKS, NUM_CHMAPS,
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
  if (cmd.start_id() >= NUM_STREAMS ||
      cmd.start_id() + cmd.count() > NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG, {});
    return;
  }
  std::vector<virtio_snd_pcm_info> stream_info(
      &STREAMS[cmd.start_id()], &STREAMS[0] + cmd.start_id() + cmd.count());
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK, stream_info);
}

void AudioHandler::SetStreamParameters(StreamSetParamsCommand& cmd) {
  if (cmd.stream_id() >= NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  const auto& stream_info = STREAMS[cmd.stream_id()];
  auto bits_per_sample = BitsPerSample(cmd.format());
  auto sample_rate = SampleRate(cmd.rate());
  auto channels = cmd.channels();
  if (bits_per_sample < 0 || sample_rate < 0 ||
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
    auto len10ms = (channels * (sample_rate / 100) * bits_per_sample) / 8;
    stream_descs_[cmd.stream_id()].buffer.Reset(len10ms);
  }
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::PrepareStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::ReleaseStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::StartStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  stream_descs_[cmd.stream_id()].active = true;
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::StopStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  stream_descs_[cmd.stream_id()].active = false;
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::ChmapsInfo(ChmapInfoCommand& cmd) {
  if (cmd.start_id() >= NUM_CHMAPS ||
      cmd.start_id() + cmd.count() > NUM_CHMAPS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG, {});
    return;
  }
  std::vector<virtio_snd_chmap_info> chmap_info(
      &CHMAPS[cmd.start_id()], &CHMAPS[cmd.start_id()] + cmd.count());
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK, chmap_info);
}

void AudioHandler::JacksInfo(JackInfoCommand& cmd) {
  if (cmd.start_id() >= NUM_JACKS ||
      cmd.start_id() + cmd.count() > NUM_JACKS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG, {});
    return;
  }
  std::vector<virtio_snd_jack_info> jack_info(
      &JACKS[cmd.start_id()], &JACKS[cmd.start_id()] + cmd.count());
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK, jack_info);
}

void AudioHandler::OnPlaybackBuffer(TxBuffer buffer) {
  auto stream_id = buffer.stream_id();
  auto& stream_desc = stream_descs_[stream_id];
  {
    std::lock_guard<std::mutex> lock(stream_desc.mtx);
    auto& holding_buffer = stream_descs_[stream_id].buffer;
    // Invalid or capture streams shouldn't send tx buffers
    if (stream_id >= NUM_STREAMS || IsCapture(stream_id)) {
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_BAD_MSG, 0, 0);
      return;
    }
    // A buffer may be received for an inactive stream if we were slow to
    // process it and the other side stopped the stream. Quitely ignore it in
    // that case
    if (!stream_desc.active) {
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
      return;
    }
    // Webrtc will silently ignore any buffer with a length different than 10ms,
    // so we must split any buffer bigger than that and temporarily store any
    // remaining frames that are less than that size.
    auto current_time = rtc::TimeMillis();
    // The timestamp of the first 10ms chunk to be sent so that the last one
    // will have the current time
    auto base_time =
        current_time - ((buffer.len() - 1) / holding_buffer.buffer.size()) * 10;
    // number of frames in a 10 ms buffer
    const int frames = stream_desc.sample_rate / 100;
    size_t pos = 0;
    while (pos < buffer.len()) {
      if (holding_buffer.empty() &&
          buffer.len() - pos >= holding_buffer.buffer.size()) {
        // Avoid the extra copy into holding buffer
        // This casts away volatility of the pointer, necessary because the
        // webrtc api doesn't expect volatile memory. This should be safe though
        // because webrtc will use the contents of the buffer before returning
        // and only then we release it.
        auto audio_frame_buffer = std::make_shared<CvdAudioFrameBuffer>(
            const_cast<const uint8_t*>(&buffer.get()[pos]),
            stream_desc.bits_per_sample, stream_desc.sample_rate,
            stream_desc.channels, frames);
        audio_sink_->OnFrame(audio_frame_buffer, base_time);
        pos += holding_buffer.buffer.size();
      } else {
        pos += holding_buffer.Add(buffer.get() + pos, buffer.len() - pos);
        if (holding_buffer.full()) {
          auto buffer_ptr = const_cast<const uint8_t*>(holding_buffer.data());
          auto audio_frame_buffer = std::make_shared<CvdAudioFrameBuffer>(
              buffer_ptr, stream_desc.bits_per_sample,
              stream_desc.sample_rate, stream_desc.channels, frames);
          audio_sink_->OnFrame(audio_frame_buffer, base_time);
          holding_buffer.count = 0;
        }
      }
      base_time += 10;
    }
  }
  buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
}

void AudioHandler::OnCaptureBuffer(RxBuffer buffer) {
  auto stream_id = buffer.stream_id();
  auto& stream_desc = stream_descs_[stream_id];
  {
    std::lock_guard<std::mutex> lock(stream_desc.mtx);
    // Invalid or playback streams shouldn't send rx buffers
    if (stream_id >= NUM_STREAMS || !IsCapture(stream_id)) {
      LOG(ERROR) << "Received capture buffers on playback stream " << stream_id;
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_BAD_MSG, 0, 0);
      return;
    }
    // A buffer may be received for an inactive stream if we were slow to
    // process it and the other side stopped the stream. Quitely ignore it in
    // that case
    if (!stream_desc.active) {
      buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
      return;
    }
    auto bytes_per_sample = stream_desc.bits_per_sample / 8;
    auto samples_per_channel =
        buffer.len() / stream_desc.channels / bytes_per_sample;
    bool muted = false;
    auto res = audio_source_->GetMoreAudioData(
        const_cast<uint8_t*>(buffer.get()), bytes_per_sample,
        samples_per_channel, stream_desc.channels, stream_desc.sample_rate,
        muted);
    if (res < 0) {
      // This is likely a recoverable error, log the error but don't let the VMM
      // know about it so that it doesn't crash.
      LOG(ERROR) << "Failed to receive audio data from client";
    }
  }
  buffer.SendStatus(AudioStatus::VIRTIO_SND_S_OK, 0, buffer.len());
}

void AudioHandler::HoldingBuffer::Reset(size_t size) {
  buffer.resize(size);
  count = 0;
}

size_t AudioHandler::HoldingBuffer::Add(const volatile uint8_t* data,
                                        size_t max_len) {
  auto added_len = std::min(max_len, buffer.size() - count);
  std::copy(data, data + added_len, &buffer[count]);
  count += added_len;
  return added_len;
}

bool AudioHandler::HoldingBuffer::empty() const { return count == 0; }

bool AudioHandler::HoldingBuffer::full() const {
  return count == buffer.size();
}

uint8_t* AudioHandler::HoldingBuffer::data() { return buffer.data(); }

}  // namespace cuttlefish
