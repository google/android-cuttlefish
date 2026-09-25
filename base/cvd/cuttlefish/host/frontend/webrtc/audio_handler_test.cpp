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

#include "cuttlefish/host/frontend/webrtc/audio_handler.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/frontend/webrtc/audio_settings.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/audio_source.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/audio_frame_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/audio_sink.h"
#include "cuttlefish/host/libs/audio_connector/buffers.h"
#include "cuttlefish/host/libs/audio_connector/commands.h"
#include "cuttlefish/host/libs/audio_connector/server.h"
#include "cuttlefish/host/libs/audio_connector/shm_layout.h"

namespace cuttlefish {
namespace {

// virtio-snd stream ids as AudioHandler numbers them: capture streams first,
// then playback streams offset by the capture count.
constexpr uint32_t kDefaultStreamId = 0;
constexpr uint32_t kOverriddenStreamId = 1;
constexpr uint32_t kPlaybackStreamId = 2;

constexpr int kChannels = 2;
constexpr size_t kSamplesPer10Ms = 480;  // 48 kHz
constexpr size_t k10MsBytes = kSamplesPer10Ms * kChannels * sizeof(int16_t);

constexpr int16_t kDefaultSample = 1111;
constexpr int16_t kOverrideSample = 2222;

// Fills every requested sample with a fixed value and records calls.
class FakeAudioSource : public webrtc_streaming::AudioSource {
 public:
  explicit FakeAudioSource(int16_t sample) : sample_(sample) {}

  int GetMoreAudioData(void* data, int bytes_per_sample,
                       int samples_per_channel, int num_channels,
                       int sample_rate, bool& muted) override {
    ++pull_count_;
    muted = false;
    int16_t* samples = static_cast<int16_t*>(data);
    for (int i = 0; i < samples_per_channel * num_channels; ++i) {
      samples[i] = sample_;
    }
    return samples_per_channel;
  }

  void Reset() override { ++reset_count_; }

  int pull_count() const { return pull_count_; }
  int reset_count() const { return reset_count_; }

 private:
  const int16_t sample_;
  int pull_count_ = 0;
  int reset_count_ = 0;
};

class NullAudioSink : public webrtc_streaming::AudioSink {
 public:
  void OnFrame(const webrtc_streaming::AudioFrameBuffer&, int64_t) override {}
};

std::vector<AudioStreamSettings> StreamSettings() {
  return {
      {.id = 0, .direction = AudioStreamSettings::Direction::Capture},
      {.id = 1, .direction = AudioStreamSettings::Direction::Capture},
      {.id = 0, .direction = AudioStreamSettings::Direction::Playback},
  };
}

// Start() is never called, so the server socket is never used.
std::unique_ptr<AudioHandler> CreateHandler(
    std::shared_ptr<webrtc_streaming::AudioSource> default_source) {
  return std::make_unique<AudioHandler>(
      std::make_unique<AudioServer>(SharedFD()),
      std::make_shared<NullAudioSink>(), std::move(default_source),
      StreamSettings(), AudioMixerSettings{});
}

void StartStream(AudioHandler& handler, uint32_t stream_id) {
  StreamControlCommand cmd(AudioCommandType::VIRTIO_SND_R_PCM_START, stream_id);
  handler.StartStream(cmd);
  EXPECT_EQ(cmd.status(), AudioStatus::VIRTIO_SND_S_OK);
}

void ConfigureAndStart(AudioHandler& handler, uint32_t stream_id) {
  StreamSetParamsCommand params(
      stream_id, /*buffer_bytes=*/4 * k10MsBytes, /*period_bytes=*/k10MsBytes,
      /*features=*/0, kChannels,
      static_cast<uint8_t>(AudioStreamFormat::VIRTIO_SND_PCM_FMT_S16),
      static_cast<uint8_t>(AudioStreamRate::VIRTIO_SND_PCM_RATE_48000));
  handler.SetStreamParameters(params);
  EXPECT_EQ(params.status(), AudioStatus::VIRTIO_SND_S_OK);
  StartStream(handler, stream_id);
}

// Sends one 10 ms capture buffer for `stream_id` and returns what was written.
std::vector<int16_t> Capture(AudioHandler& handler, uint32_t stream_id) {
  std::vector<uint8_t> shm(k10MsBytes, 0);
  AudioStatus status = AudioStatus::NOT_SET;
  RxBuffer buffer(virtio_snd_pcm_xfer{.stream_id = Le32(stream_id)}, shm.data(),
                  shm.size(),
                  [&status](AudioStatus s, uint32_t, uint32_t) { status = s; });
  handler.OnCaptureBuffer(std::move(buffer));
  EXPECT_EQ(status, AudioStatus::VIRTIO_SND_S_OK);

  std::vector<int16_t> samples(shm.size() / sizeof(int16_t));
  std::memcpy(samples.data(), shm.data(), shm.size());
  return samples;
}

TEST(AudioHandlerRoutingTest, OverriddenStreamUsesItsOwnSource) {
  std::shared_ptr<FakeAudioSource> default_source =
      std::make_shared<FakeAudioSource>(kDefaultSample);
  std::shared_ptr<FakeAudioSource> override_source =
      std::make_shared<FakeAudioSource>(kOverrideSample);
  std::unique_ptr<AudioHandler> handler = CreateHandler(default_source);
  handler->SetCaptureSource(kOverriddenStreamId, override_source);
  ConfigureAndStart(*handler, kDefaultStreamId);
  ConfigureAndStart(*handler, kOverriddenStreamId);

  EXPECT_EQ(Capture(*handler, kOverriddenStreamId),
            std::vector<int16_t>(kSamplesPer10Ms * kChannels, kOverrideSample));
  EXPECT_EQ(override_source->pull_count(), 1);
  EXPECT_EQ(default_source->pull_count(), 0);

  EXPECT_EQ(Capture(*handler, kDefaultStreamId),
            std::vector<int16_t>(kSamplesPer10Ms * kChannels, kDefaultSample));
  EXPECT_EQ(default_source->pull_count(), 1);
  EXPECT_EQ(override_source->pull_count(), 1);
}

TEST(AudioHandlerRoutingTest, StreamWithoutOverrideUsesDefaultSource) {
  std::shared_ptr<FakeAudioSource> default_source =
      std::make_shared<FakeAudioSource>(kDefaultSample);
  std::unique_ptr<AudioHandler> handler = CreateHandler(default_source);
  ConfigureAndStart(*handler, kOverriddenStreamId);

  EXPECT_EQ(Capture(*handler, kOverriddenStreamId),
            std::vector<int16_t>(kSamplesPer10Ms * kChannels, kDefaultSample));
  EXPECT_EQ(default_source->pull_count(), 1);
}

TEST(AudioHandlerRoutingTest, StartStreamResetsOnlyThatStreamsSource) {
  std::shared_ptr<FakeAudioSource> default_source =
      std::make_shared<FakeAudioSource>(kDefaultSample);
  std::shared_ptr<FakeAudioSource> override_source =
      std::make_shared<FakeAudioSource>(kOverrideSample);
  std::unique_ptr<AudioHandler> handler = CreateHandler(default_source);
  handler->SetCaptureSource(kOverriddenStreamId, override_source);

  StartStream(*handler, kOverriddenStreamId);
  EXPECT_EQ(override_source->reset_count(), 1);
  EXPECT_EQ(default_source->reset_count(), 0);

  StartStream(*handler, kDefaultStreamId);
  EXPECT_EQ(default_source->reset_count(), 1);
  EXPECT_EQ(override_source->reset_count(), 1);

  // Playback streams have no capture source to reset.
  StartStream(*handler, kPlaybackStreamId);
  EXPECT_EQ(default_source->reset_count(), 1);
  EXPECT_EQ(override_source->reset_count(), 1);
}

TEST(AudioHandlerRoutingDeathTest, RejectsOverrideForPlaybackStream) {
  EXPECT_DEATH(
      {
        std::unique_ptr<AudioHandler> handler =
            CreateHandler(std::make_shared<FakeAudioSource>(kDefaultSample));
        handler->SetCaptureSource(
            kPlaybackStreamId,
            std::make_shared<FakeAudioSource>(kOverrideSample));
      },
      "is not a capture stream");
}

}  // namespace
}  // namespace cuttlefish
