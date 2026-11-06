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

#include <gtest/gtest.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/pcm_stream_server.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

bool ReadExactly(const SharedFD& fd, std::span<uint8_t> buffer) {
  return cuttlefish::ReadExact(fd, reinterpret_cast<char*>(buffer.data()),
                               buffer.size()) ==
         static_cast<ssize_t>(buffer.size());
}

constexpr int16_t kNoiseAmplitude = 4000;

TunerStateSnapshot TunedSnapshot() {
  return TunerStateSnapshot{RadioBand::FM, 88500000, 0, /* is_playing= */ true};
}

bool AnyNonZero(std::span<const uint8_t> buffer) {
  return std::any_of(buffer.begin(), buffer.end(),
                     [](uint8_t byte) { return byte != 0; });
}

bool AllSamplesZero(std::span<const int16_t> samples) {
  return std::all_of(samples.begin(), samples.end(),
                     [](int16_t sample) { return sample == 0; });
}

bool AllSamplesWithinAmplitude(std::span<const int16_t> samples,
                               int16_t amplitude) {
  return std::all_of(samples.begin(), samples.end(), [amplitude](int16_t s) {
    return s >= -amplitude && s <= amplitude;
  });
}

bool EveryFrameIsMono(std::span<const int16_t> samples) {
  for (size_t frame = 0; frame + kChannels <= samples.size();
       frame += kChannels) {
    for (size_t channel = 1; channel < kChannels; ++channel) {
      if (samples[frame + channel] != samples[frame]) {
        return false;
      }
    }
  }
  return true;
}

TEST(AudioGeneratorTest, AudioConstantsAreStandard) {
  EXPECT_EQ(kSampleRate, 48000u);
  EXPECT_EQ(kChannels, 2u);
  EXPECT_EQ(kBytesPerSample, 2u);
  EXPECT_EQ(kFrameSizeBytes, 4u);
  EXPECT_EQ(kChunkFrames, 4096u);
  EXPECT_EQ(kChunkSizeBytes, 16384u);
}

TEST(AudioGeneratorTest, GenerateChunkSilenceWhenUntuned) {
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels, 0x55);

  const TunerStateSnapshot untuned{RadioBand::FM, 0, 0,
                                   /* is_playing= */ false};
  generator.GenerateChunk(buffer, untuned);

  EXPECT_TRUE(AllSamplesZero(buffer));
}

TEST(AudioGeneratorTest, GenerateChunkNoiseWhenPlaying) {
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels, 0);

  generator.GenerateChunk(buffer, TunedSnapshot());

  EXPECT_FALSE(AllSamplesZero(buffer));
  EXPECT_TRUE(AllSamplesWithinAmplitude(buffer, kNoiseAmplitude));
  EXPECT_TRUE(EveryFrameIsMono(buffer));
}

TEST(AudioGeneratorTest, GeneratorsAreIndependent) {
  AudioGenerator first;
  AudioGenerator second;
  std::vector<int16_t> first_buffer(kChunkFrames * kChannels, 0);
  std::vector<int16_t> second_buffer(kChunkFrames * kChannels, 0);

  first.GenerateChunk(first_buffer, TunedSnapshot());
  second.GenerateChunk(second_buffer, TunedSnapshot());

  EXPECT_NE(first_buffer, second_buffer);
}

// Goertzel power of `tone_hz` in the first channel of `samples`.
double TonePower(std::span<const int16_t> samples, double tone_hz) {
  const double coeff =
      2.0 * std::cos(2.0 * std::numbers::pi * tone_hz / kSampleRate);
  double s1 = 0.0;
  double s2 = 0.0;
  for (size_t frame = 0; frame + kChannels <= samples.size();
       frame += kChannels) {
    const double s0 = samples[frame] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Programs of the guest's default AM/FM and DAB VirtualRadios
// (hardware/interfaces/broadcastradio/aidl/default/VirtualRadio.cpp).
std::vector<TunerStateSnapshot> DefaultVirtualRadioPrograms() {
  std::vector<TunerStateSnapshot> programs;
  for (uint32_t khz :
       {94900u, 96500u, 101300u, 103700u, 106100u, 97300u, 99700u}) {
    programs.push_back({RadioBand::FM, khz * 1000, 0, true});
  }
  for (uint32_t subchannel : {0u, 1u, 2u}) {
    programs.push_back({RadioBand::FM, 97700000, subchannel, true});
  }
  for (uint32_t subchannel : {0u, 1u}) {
    programs.push_back({RadioBand::FM, 98500000, subchannel, true});
  }
  for (uint32_t khz : {560u, 680u, 1170u}) {
    programs.push_back({RadioBand::AM, khz * 1000, 0, true});
  }
  for (uint32_t khz : {218640u, 222064u, 223936u, 225648u}) {
    programs.push_back({RadioBand::DAB, khz * 1000, 0, true});
  }
  return programs;
}

TEST(AudioGeneratorTest, DefaultStationsHaveDistinctTones) {
  std::vector<double> tones;
  for (const TunerStateSnapshot& program : DefaultVirtualRadioPrograms()) {
    tones.push_back(StationToneHz(program));
  }
  std::sort(tones.begin(), tones.end());
  ASSERT_GT(tones.front(), 0.0);
  for (size_t i = 1; i < tones.size(); ++i) {
    EXPECT_GE(tones[i] - tones[i - 1], 20.0)
        << "tones " << tones[i - 1] << " and " << tones[i];
  }
}

TEST(AudioGeneratorTest, BandsHaveNonOverlappingToneRanges) {
  EXPECT_LT(StationToneHz({RadioBand::AM, 1700000, 0, true}),
            StationToneHz({RadioBand::FM, 87500000, 0, true}));
  EXPECT_LT(StationToneHz({RadioBand::FM, 108000000, 1, true}),
            StationToneHz({RadioBand::DAB, 174000000, 0, true}));
}

TEST(AudioGeneratorTest, NoToneWithoutFrequency) {
  EXPECT_EQ(StationToneHz({RadioBand::AM, 0, 0, true}), 0.0);
  EXPECT_EQ(StationToneHz({RadioBand::FM, 0, 0, true}), 0.0);
  EXPECT_EQ(StationToneHz({RadioBand::DAB, 0, 0, true}), 0.0);
}

TEST(AudioGeneratorTest, PlaysWhiteNoiseWithoutFrequency) {
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels, 0);

  generator.GenerateChunk(buffer,
                          {RadioBand::FM, 0, 0, /* is_playing= */ true});

  EXPECT_FALSE(AllSamplesZero(buffer));
  EXPECT_TRUE(AllSamplesWithinAmplitude(buffer, kNoiseAmplitude));
  EXPECT_TRUE(EveryFrameIsMono(buffer));
}

TEST(AudioGeneratorTest, ChunkCarriesStationTone) {
  const TunerStateSnapshot kiss_fm{RadioBand::FM, 101300000, 0, true};
  const TunerStateSnapshot koit{RadioBand::FM, 96500000, 0, true};
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels, 0);

  generator.GenerateChunk(buffer, kiss_fm);

  EXPECT_GT(TonePower(buffer, StationToneHz(kiss_fm)),
            100.0 * TonePower(buffer, StationToneHz(koit)));
}

class PcmStreamServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Avoid testing::TempDir() which exceeds the 108-byte sockaddr_un limit
    // under Bazel.
    std::string directory_template = "/tmp/cf_virtual_tuner_XXXXXX";
    ASSERT_NE(::mkdtemp(directory_template.data()), nullptr);
    temp_dir_ = directory_template;
    socket_path_ = temp_dir_ + "/pcm.sock";
  }

  void TearDown() override {
    ::unlink(socket_path_.c_str());
    ::rmdir(temp_dir_.c_str());
  }

  SharedFD CreateServerSocket() {
    SharedFD sock = SharedFD::SocketLocalServer(
        socket_path_, /* is_abstract= */ false, SOCK_STREAM, 0600);
    EXPECT_TRUE(sock->IsOpen());
    return sock;
  }

  std::string temp_dir_;
  std::string socket_path_;
};

TEST_F(PcmStreamServerTest, StreamsAudioToConnectedClient) {
  TunerState state;
  state.SetTune(RadioBand::FM, 88500000, 0);

  PcmStreamServer server(state, CreateServerSocket());
  ASSERT_TRUE(server.Start().has_value());
  EXPECT_TRUE(server.IsRunning());

  SharedFD client =
      SharedFD::SocketLocalClient(socket_path_, false, SOCK_STREAM);
  ASSERT_TRUE(client->IsOpen()) << "connect failed: " << client->StrError();

  std::vector<uint8_t> chunk(kChunkSizeBytes);
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(ReadExactly(client, chunk)) << "short read on chunk " << i;
    EXPECT_TRUE(AnyNonZero(chunk)) << "expected audio, got silence";
  }

  client->Close();
  server.Stop();
  EXPECT_FALSE(server.IsRunning());
}

TEST_F(PcmStreamServerTest, ServesMultipleClientsConcurrently) {
  TunerState state;
  state.SetTune(RadioBand::FM, 88500000, 0);

  PcmStreamServer server(state, CreateServerSocket());
  ASSERT_TRUE(server.Start().has_value());

  SharedFD first =
      SharedFD::SocketLocalClient(socket_path_, false, SOCK_STREAM);
  SharedFD second =
      SharedFD::SocketLocalClient(socket_path_, false, SOCK_STREAM);
  ASSERT_TRUE(first->IsOpen()) << first->StrError();
  ASSERT_TRUE(second->IsOpen()) << second->StrError();

  std::vector<uint8_t> chunk(kChunkSizeBytes);
  EXPECT_TRUE(ReadExactly(first, chunk));
  EXPECT_TRUE(AnyNonZero(chunk));
  EXPECT_TRUE(ReadExactly(second, chunk));
  EXPECT_TRUE(AnyNonZero(chunk));

  first->Close();
  second->Close();
}

TEST_F(PcmStreamServerTest, UntunedClientReceivesSilence) {
  TunerState state;

  PcmStreamServer server(state, CreateServerSocket());
  ASSERT_TRUE(server.Start().has_value());

  SharedFD client =
      SharedFD::SocketLocalClient(socket_path_, false, SOCK_STREAM);
  ASSERT_TRUE(client->IsOpen()) << client->StrError();

  std::vector<uint8_t> chunk(kChunkSizeBytes);
  ASSERT_TRUE(ReadExactly(client, chunk));
  EXPECT_FALSE(AnyNonZero(chunk));

  client->Close();
}

TEST_F(PcmStreamServerTest, DestructorDisconnectsActiveClient) {
  TunerState state;
  state.SetTune(RadioBand::FM, 88500000, 0);

  SharedFD client;
  {
    PcmStreamServer server(state, CreateServerSocket());
    ASSERT_TRUE(server.Start().has_value());

    client = SharedFD::SocketLocalClient(socket_path_, false, SOCK_STREAM);
    ASSERT_TRUE(client->IsOpen()) << client->StrError();

    std::vector<uint8_t> chunk(kChunkSizeBytes);
    ASSERT_TRUE(ReadExactly(client, chunk));
  }

  std::vector<uint8_t> drain(kChunkSizeBytes);
  Result<uint64_t> bytes_read = client->Read(drain.data(), drain.size());
  while (bytes_read.has_value() && *bytes_read > 0) {
    bytes_read = client->Read(drain.data(), drain.size());
  }
  ASSERT_TRUE(bytes_read.has_value()) << bytes_read.error().FormatForEnv();
  EXPECT_EQ(*bytes_read, 0u) << "expected EOF after server teardown";
}

TEST_F(PcmStreamServerTest, StopIsIdempotent) {
  TunerState state;
  PcmStreamServer server(state, CreateServerSocket());
  ASSERT_TRUE(server.Start().has_value());

  server.Stop();
  server.Stop();
  EXPECT_FALSE(server.IsRunning());
}

}  // namespace
}  // namespace virtualtuner
}  // namespace cuttlefish
