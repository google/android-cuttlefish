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

#include "cuttlefish/host/frontend/webrtc/tuner_audio_source.h"

#include <gtest/gtest.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/socket.h>  // IWYU pragma: keep
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace {

constexpr int kBytesPerSample = sizeof(int16_t);
constexpr int kSamplesPerChannel = 480;  // 10 ms at 48 kHz
constexpr int kChannels = 2;
constexpr int kSampleRate = 48000;
constexpr size_t kFrameBlockSamples = kSamplesPerChannel * kChannels;
constexpr size_t kFrameBlockBytes = kFrameBlockSamples * kBytesPerSample;

bool AllZero(std::span<const int16_t> samples) {
  return std::all_of(samples.begin(), samples.end(),
                     [](int16_t sample) { return sample == 0; });
}

// Fails fast instead of blocking forever if the source never connects.
SharedFD AcceptClient(const SharedFD& server) {
  PollSharedFd poll_fd{.fd = server, .events = POLLIN, .revents = 0};
  if (SharedFD::Poll(&poll_fd, 1, 1000) != 1) {
    return SharedFD();
  }
  return Fd::Accept(*server).value_or(Fd());
}

class TunerAudioSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Not TEST_TMPDIR: bazel's path can exceed the 108-byte sun_path limit.
    std::string dir_template = "/tmp/cf_tuner_src_XXXXXX";
    ASSERT_NE(::mkdtemp(dir_template.data()), nullptr);
    temp_dir_ = dir_template;
    socket_path_ = temp_dir_ + "/pcm.sock";
  }

  void TearDown() override {
    ::unlink(socket_path_.c_str());
    ::rmdir(temp_dir_.c_str());
  }

  SharedFD StartServer() {
    SharedFD server =
        SharedFD::SocketLocalServer(socket_path_, false, SOCK_STREAM, 0600);
    EXPECT_TRUE(server->IsOpen()) << server->StrError();
    return server;
  }

  std::string temp_dir_;
  std::string socket_path_;
};

TEST_F(TunerAudioSourceTest, EmitsSilenceWhenSocketAbsent) {
  TunerAudioSource source(socket_path_, kChannels);
  std::vector<int16_t> buffer(kFrameBlockSamples, 0x1234);
  bool muted = true;

  const int samples_read = source.GetMoreAudioData(
      buffer.data(), kBytesPerSample, kSamplesPerChannel, kChannels,
      kSampleRate, muted);

  EXPECT_EQ(samples_read, kSamplesPerChannel);
  EXPECT_FALSE(muted);
  EXPECT_TRUE(AllZero(buffer));
}

TEST_F(TunerAudioSourceTest, PrimesAndStreamsPcmData) {
  SharedFD server = StartServer();
  TunerAudioSource source(socket_path_, kChannels);
  SharedFD client = AcceptClient(server);
  ASSERT_TRUE(client->IsOpen()) << client->StrError();
  std::vector<int16_t> out(kFrameBlockSamples, 0);
  bool muted = false;

  // Connected, but no data is buffered yet -> silence.
  EXPECT_EQ(
      source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                              kChannels, kSampleRate, muted),
      kSamplesPerChannel);
  EXPECT_TRUE(AllZero(out));

  // Write 1 period (10 ms): still below the 2-period (20 ms) prime threshold.
  const std::vector<int16_t> period1(kFrameBlockSamples, 1111);
  ASSERT_EQ(WriteAll(client, reinterpret_cast<const char*>(period1.data()),
                     kFrameBlockBytes),
            static_cast<ssize_t>(kFrameBlockBytes));
  EXPECT_EQ(
      source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                              kChannels, kSampleRate, muted),
      kSamplesPerChannel);
  EXPECT_TRUE(AllZero(out));

  // Write 2nd period (now 20 ms queued): source becomes primed and serves
  // period1.
  const std::vector<int16_t> period2(kFrameBlockSamples, 2222);
  ASSERT_EQ(WriteAll(client, reinterpret_cast<const char*>(period2.data()),
                     kFrameBlockBytes),
            static_cast<ssize_t>(kFrameBlockBytes));

  EXPECT_EQ(
      source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                              kChannels, kSampleRate, muted),
      kSamplesPerChannel);
  EXPECT_EQ(out, period1);

  // Next pull consumes period2.
  EXPECT_EQ(
      source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                              kChannels, kSampleRate, muted),
      kSamplesPerChannel);
  EXPECT_EQ(out, period2);

  // Next pull underruns -> silence and re-primes.
  EXPECT_EQ(
      source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                              kChannels, kSampleRate, muted),
      kSamplesPerChannel);
  EXPECT_TRUE(AllZero(out));
}

TEST_F(TunerAudioSourceTest, EmitsSilenceAfterPeerClose) {
  SharedFD server = StartServer();
  TunerAudioSource source(socket_path_, kChannels);
  SharedFD client = AcceptClient(server);
  ASSERT_TRUE(client->IsOpen()) << client->StrError();
  std::vector<int16_t> out(kFrameBlockSamples, 0);
  bool muted = false;

  const std::vector<int16_t> data(2 * kFrameBlockSamples, 1111);
  ASSERT_EQ(WriteAll(client, reinterpret_cast<const char*>(data.data()),
                     2 * kFrameBlockBytes),
            static_cast<ssize_t>(2 * kFrameBlockBytes));
  source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                          kChannels, kSampleRate, muted);
  EXPECT_EQ(out[0], 1111);

  // Peer closes: buffered data is dropped and the source emits silence.
  client = SharedFD();
  source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                          kChannels, kSampleRate, muted);
  EXPECT_TRUE(AllZero(out));

  // No reconnect is attempted; the source stays silent.
  source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                          kChannels, kSampleRate, muted);
  EXPECT_TRUE(AllZero(out));
  PollSharedFd poll_fd{.fd = server, .events = POLLIN, .revents = 0};
  EXPECT_EQ(SharedFD::Poll(&poll_fd, 1, 0), 0);
}

TEST_F(TunerAudioSourceTest, OverflowTrimKeepsFrameAlignment) {
  SharedFD server = StartServer();
  TunerAudioSource source(socket_path_, kChannels);
  SharedFD client = AcceptClient(server);
  ASSERT_TRUE(client->IsOpen()) << client->StrError();
  std::vector<int16_t> out(kFrameBlockSamples, 0);
  bool muted = false;

  // 110 ms of frames (L = n, R = -n) plus a trailing partial frame, which
  // overflows the 100 ms cap with a byte count that is not frame aligned.
  constexpr int kFrames = 11 * kSamplesPerChannel;
  std::vector<int16_t> data;
  for (int n = 1; n <= kFrames; ++n) {
    data.push_back(static_cast<int16_t>(n));
    data.push_back(static_cast<int16_t>(-n));
  }
  data.push_back(0x7fff);
  const size_t data_bytes = data.size() * sizeof(int16_t);
  ASSERT_EQ(
      WriteAll(client, reinterpret_cast<const char*>(data.data()), data_bytes),
      static_cast<ssize_t>(data_bytes));

  source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                          kChannels, kSampleRate, muted);
  ASSERT_GT(out[0], 0);
  for (size_t i = 0; i < kSamplesPerChannel; ++i) {
    EXPECT_EQ(out[2 * i], out[0] + static_cast<int16_t>(i)) << i;
    EXPECT_EQ(out[2 * i + 1], -out[2 * i]) << i;
  }
}

TEST_F(TunerAudioSourceTest, EmitsSilenceForUnsupportedFormat) {
  SharedFD server = StartServer();
  TunerAudioSource source(socket_path_, kChannels);
  SharedFD client = AcceptClient(server);
  ASSERT_TRUE(client->IsOpen()) << client->StrError();
  bool muted = false;

  const std::vector<int16_t> data(2 * kFrameBlockSamples, 1111);
  ASSERT_EQ(WriteAll(client, reinterpret_cast<const char*>(data.data()),
                     2 * kFrameBlockBytes),
            static_cast<ssize_t>(2 * kFrameBlockBytes));

  // Mono request against a stereo source: the PCM can't be served as is.
  std::vector<int16_t> mono(kSamplesPerChannel, 0x1234);
  EXPECT_EQ(source.GetMoreAudioData(mono.data(), kBytesPerSample,
                                    kSamplesPerChannel, 1, kSampleRate, muted),
            kSamplesPerChannel);
  EXPECT_TRUE(AllZero(mono));
}

TEST_F(TunerAudioSourceTest, ServesConfiguredChannelCount) {
  SharedFD server = StartServer();
  TunerAudioSource source(socket_path_, 1);
  SharedFD client = AcceptClient(server);
  ASSERT_TRUE(client->IsOpen()) << client->StrError();
  bool muted = false;

  // Two mono periods are enough to prime a mono source.
  const std::vector<int16_t> data(2 * kSamplesPerChannel, 3333);
  const size_t data_bytes = data.size() * sizeof(int16_t);
  ASSERT_EQ(
      WriteAll(client, reinterpret_cast<const char*>(data.data()), data_bytes),
      static_cast<ssize_t>(data_bytes));

  std::vector<int16_t> mono(kSamplesPerChannel, 0);
  EXPECT_EQ(source.GetMoreAudioData(mono.data(), kBytesPerSample,
                                    kSamplesPerChannel, 1, kSampleRate, muted),
            kSamplesPerChannel);
  EXPECT_EQ(mono, std::vector<int16_t>(kSamplesPerChannel, 3333));
}

TEST_F(TunerAudioSourceTest, ResetDropsStaleAudio) {
  SharedFD server = StartServer();
  TunerAudioSource source(socket_path_, kChannels);
  SharedFD client = AcceptClient(server);
  ASSERT_TRUE(client->IsOpen()) << client->StrError();
  std::vector<int16_t> out(kFrameBlockSamples, 0);
  bool muted = false;

  // Prime and consume one period, leaving stale audio in the FIFO.
  const std::vector<int16_t> stale(3 * kFrameBlockSamples, 1111);
  ASSERT_EQ(WriteAll(client, reinterpret_cast<const char*>(stale.data()),
                     3 * kFrameBlockBytes),
            static_cast<ssize_t>(3 * kFrameBlockBytes));
  source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                          kChannels, kSampleRate, muted);
  ASSERT_EQ(out[0], 1111);

  // Stream restarts. Audio written in the meantime is still in the socket and
  // must be dropped together with the FIFO contents.
  source.Reset();
  ASSERT_EQ(WriteAll(client, reinterpret_cast<const char*>(stale.data()),
                     2 * kFrameBlockBytes),
            static_cast<ssize_t>(2 * kFrameBlockBytes));
  source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                          kChannels, kSampleRate, muted);
  EXPECT_TRUE(AllZero(out));

  // Fresh audio is served once the source re-primes.
  const std::vector<int16_t> fresh(2 * kFrameBlockSamples, 2222);
  ASSERT_EQ(WriteAll(client, reinterpret_cast<const char*>(fresh.data()),
                     2 * kFrameBlockBytes),
            static_cast<ssize_t>(2 * kFrameBlockBytes));
  source.GetMoreAudioData(out.data(), kBytesPerSample, kSamplesPerChannel,
                          kChannels, kSampleRate, muted);
  EXPECT_EQ(out, std::vector<int16_t>(kFrameBlockSamples, 2222));
}

}  // namespace
}  // namespace cuttlefish
