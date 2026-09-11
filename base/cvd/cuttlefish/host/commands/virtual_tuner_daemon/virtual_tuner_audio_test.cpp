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
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

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
  size_t offset = 0;
  while (offset < buffer.size()) {
    Result<uint64_t> bytes_read =
        fd->Read(buffer.data() + offset, buffer.size() - offset);
    if (!bytes_read.has_value() || *bytes_read == 0) {
      return false;
    }
    offset += *bytes_read;
  }
  return true;
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

  std::string temp_dir_;
  std::string socket_path_;
};

TEST_F(PcmStreamServerTest, StreamsAudioToConnectedClient) {
  TunerState state;
  state.SetTune(RadioBand::FM, 88500000, 0);

  PcmStreamServer server(&state, socket_path_);
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

  PcmStreamServer server(&state, socket_path_);
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

  PcmStreamServer server(&state, socket_path_);
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
    PcmStreamServer server(&state, socket_path_);
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

TEST_F(PcmStreamServerTest, StopRemovesSocketFile) {
  TunerState state;
  PcmStreamServer server(&state, socket_path_);
  ASSERT_TRUE(server.Start().has_value());
  ASSERT_EQ(::access(socket_path_.c_str(), F_OK), 0);

  server.Stop();

  EXPECT_NE(::access(socket_path_.c_str(), F_OK), 0)
      << "socket file should not outlive the server";
}

TEST_F(PcmStreamServerTest, StopIsIdempotent) {
  TunerState state;
  PcmStreamServer server(&state, socket_path_);
  ASSERT_TRUE(server.Start().has_value());

  server.Stop();
  server.Stop();
  EXPECT_FALSE(server.IsRunning());
}

}  // namespace
}  // namespace virtualtuner
}  // namespace cuttlefish
