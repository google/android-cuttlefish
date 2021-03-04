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

const uint32_t NUM_STREAMS = 1;
const virtio_snd_pcm_info STREAM_INFO = {
    .hdr =
        {
            .hda_fn_nid = Le32(0),
        },
    .features = Le32(0),
    // webrtc's api is quite primitive and doesn't allow for many different
    // formats: It only takes the bits_per_sample as a parameter and assumes
    // the underlying format to be one of the following:
    .formats = Le64(
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U8) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U16) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U24) |
        (((uint64_t)1) << (uint8_t)AudioStreamFormat::VIRTIO_SND_PCM_FMT_U32)),
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
};

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
    std::shared_ptr<webrtc_streaming::AudioSink> audio_sink,
    std::unique_ptr<AudioServer> audio_server)
    : audio_sink_(audio_sink),
      audio_server_(std::move(audio_server)),
      stream_parameters_({{StreamParameters()}}),
      stream_buffers_({{HoldingBuffer()}}) {}

void AudioHandler::Start() {
  server_thread_ = std::thread([this]() { Loop(); });
}

[[noreturn]] void AudioHandler::Loop() {
  for (;;) {
    auto audio_client = audio_server_->AcceptClient(
        1 /* num_streams, */, 0 /* num_jacks, */, 0 /* num_chmaps, */,
        262144 /* tx_shm_len */, 262144 /* rx_shm_len */);
    CHECK(audio_client) << "Failed to create audio client connection instance";

    // Wait for the client to do something
    while (audio_client->ReceivePending(*this)) {
    }
  }
}

void AudioHandler::StreamsInfo(StreamInfoCommand& cmd) {
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK, {STREAM_INFO});
}

void AudioHandler::SetStreamParameters(StreamSetParamsCommand& cmd) {
  if (cmd.stream_id() >= NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  auto bits_per_sample = BitsPerSample(cmd.format());
  auto sample_rate = SampleRate(cmd.rate());
  auto channels = cmd.channels();
  if (bits_per_sample < 0 || sample_rate < 0 ||
      channels < STREAM_INFO.channels_min ||
      channels > STREAM_INFO.channels_max) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  stream_parameters_[cmd.stream_id()].bits_per_sample = bits_per_sample;
  stream_parameters_[cmd.stream_id()].sample_rate = sample_rate;
  stream_parameters_[cmd.stream_id()].channels = channels;
  auto len10ms = (channels * (sample_rate / 100) * bits_per_sample) / 8;
  stream_buffers_[cmd.stream_id()].Reset(len10ms);
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
  stream_parameters_[cmd.stream_id()].active = true;
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::StopStream(StreamControlCommand& cmd) {
  if (cmd.stream_id() >= NUM_STREAMS) {
    cmd.Reply(AudioStatus::VIRTIO_SND_S_BAD_MSG);
    return;
  }
  stream_parameters_[cmd.stream_id()].active = false;
  cmd.Reply(AudioStatus::VIRTIO_SND_S_OK);
}

void AudioHandler::OnBuffer(TxBuffer buffer) {
  auto stream_id = buffer.stream_id();
  auto& stream_params = stream_parameters_[stream_id];
  auto& holding_buffer = stream_buffers_[stream_id];
  // Unknown, inactive or capture streams shouldn't send buffers
  if (stream_id >= NUM_STREAMS || !stream_params.active ||
      stream_params.capture) {
    buffer.SendStatus(AudioStatus::VIRTIO_SND_S_BAD_MSG, 0, 0);
    return;
  }
  // Webrtc will silently ignore any buffer with a length different than 10ms,
  // so we must split any buffer bigger than that and temporarily store any
  // remaining frames that are less than that size.
  auto current_time = rtc::TimeMillis();
  // The timestamp of the first 10ms chunk to be sent so that the last one will
  // have the current time
  auto base_time =
      current_time - ((buffer.len() - 1) / holding_buffer.buffer.size()) * 10;
  // number of frames in a 10 ms buffer
  const int frames = stream_params.sample_rate / 100;
  size_t pos = 0;
  while (pos < buffer.len()) {
    if (holding_buffer.empty() &&
        buffer.len() - pos >= holding_buffer.buffer.size()) {
      // Avoid the extra copy into holding buffer
      // This casts away volatility of the pointer, necessary because the
      // webrtc api doesn't expect volatile memory. This should be safe though
      // because webrtc will use the contents of the buffer before returning and
      // only then we release it.
      auto audio_frame_buffer = std::make_shared<CvdAudioFrameBuffer>(
          const_cast<const uint8_t*>(&buffer.get()[pos]),
          stream_params.bits_per_sample, stream_params.sample_rate,
          stream_params.channels, frames);
      audio_sink_->OnFrame(audio_frame_buffer, base_time);
      pos += holding_buffer.buffer.size();
    } else {
      pos += holding_buffer.Add(buffer.get() + pos, buffer.len() - pos);
      if (holding_buffer.full()) {
        auto buffer_ptr = const_cast<const uint8_t*>(holding_buffer.data());
        auto audio_frame_buffer = std::make_shared<CvdAudioFrameBuffer>(
            buffer_ptr, stream_params.bits_per_sample,
            stream_params.sample_rate, stream_params.channels, frames);
        audio_sink_->OnFrame(audio_frame_buffer, base_time);
        holding_buffer.count = 0;
      }
    }
    base_time += 10;
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
