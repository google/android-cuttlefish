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

#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_service.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.grpc.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_control_service.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

std::vector<uint8_t> BuildStereoTestWav(std::span<const int16_t> samples) {
  const uint32_t data_bytes =
      static_cast<uint32_t>(samples.size() * sizeof(int16_t));
  const uint32_t riff_size = 36u + data_bytes;
  std::vector<uint8_t> bytes;
  auto push_bytes = [&](const void* ptr, size_t n) {
    const auto* p = static_cast<const uint8_t*>(ptr);
    bytes.insert(bytes.end(), p, p + n);
  };
  auto push_u16 = [&](uint16_t v) {
    bytes.push_back(static_cast<uint8_t>(v & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
  };
  auto push_u32 = [&](uint32_t v) {
    push_u16(static_cast<uint16_t>(v & 0xFFFFu));
    push_u16(static_cast<uint16_t>((v >> 16) & 0xFFFFu));
  };

  push_bytes("RIFF", 4);
  push_u32(riff_size);
  push_bytes("WAVE", 4);
  push_bytes("fmt ", 4);
  push_u32(16u);
  push_u16(1u);
  push_u16(static_cast<uint16_t>(kChannels));
  push_u32(static_cast<uint32_t>(kSampleRate));
  push_u32(static_cast<uint32_t>(kSampleRate * kFrameSizeBytes));
  push_u16(static_cast<uint16_t>(kFrameSizeBytes));
  push_u16(16u);
  push_bytes("data", 4);
  push_u32(data_bytes);
  for (int16_t sample : samples) {
    push_u16(static_cast<uint16_t>(sample));
  }
  return bytes;
}

TEST(VirtualTunerServiceTest, GrpcTuneAndStop) {
  TunerState state;
  VirtualTunerServiceImpl service(state);

  ::grpc::ServerBuilder builder;
  builder.RegisterService(&service);
  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  ASSERT_NE(server, nullptr);

  std::unique_ptr<VirtualTuner::Stub> stub = VirtualTuner::NewStub(
      server->InProcessChannel(::grpc::ChannelArguments()));

  // 1. Tune RPC
  {
    ::grpc::ClientContext context;
    TuneRequest request;
    request.set_band(RadioBand::FM);
    request.set_frequency_hz(96500000);
    request.set_hd_subchannel(0);

    TuneResponse response;
    ::grpc::Status status = stub->Tune(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(response.success());
    EXPECT_EQ(state.GetFrequency(), 96500000u);
    EXPECT_TRUE(state.IsPlaying());
  }

  // 2. GetStatus & Seek RPCs
  {
    ::grpc::ClientContext status_ctx;
    GetStatusRequest status_req;
    GetStatusResponse status_resp;
    EXPECT_TRUE(stub->GetStatus(&status_ctx, status_req, &status_resp).ok());
    EXPECT_TRUE(status_resp.status().is_playing());
    EXPECT_EQ(status_resp.status().current_station().program_name(), "KOIT");

    ::grpc::ClientContext seek_ctx;
    SeekRequest seek_req;
    seek_req.set_band(RadioBand::FM);
    seek_req.set_start_frequency_hz(96500000);
    seek_req.set_direction_up(true);
    seek_req.set_skip_subchannel(false);
    SeekResponse seek_resp;
    EXPECT_TRUE(stub->Seek(&seek_ctx, seek_req, &seek_resp).ok());
    EXPECT_TRUE(seek_resp.has_station());
    EXPECT_EQ(seek_resp.station().frequency_hz(), 97300000u);
  }

  // 3. Stop RPC
  {
    ::grpc::ClientContext context;
    StopRequest request;
    StopResponse response;
    ::grpc::Status status = stub->Stop(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(response.success());
    EXPECT_FALSE(state.IsPlaying());
    EXPECT_EQ(state.GetFrequency(), 0u);
  }

  server->Shutdown();
}

TEST(VirtualTunerServiceTest,
     GuestServiceSupportsStationControlsWithoutSetStationAudio) {
  TunerState state;
  VirtualTunerServiceImpl guest_service(state);

  ::grpc::ServerBuilder builder;
  builder.RegisterService(&guest_service);
  std::unique_ptr<::grpc::Server> guest_server(builder.BuildAndStart());
  ASSERT_NE(guest_server, nullptr);

  std::unique_ptr<VirtualTuner::Stub> guest_stub = VirtualTuner::NewStub(
      guest_server->InProcessChannel(::grpc::ChannelArguments()));

  {
    ::grpc::ClientContext upsert_ctx;
    UpsertStationRequest upsert_req;
    upsert_req.mutable_station()->set_band(RadioBand::FM);
    upsert_req.mutable_station()->set_frequency_hz(89100000);
    upsert_req.mutable_station()->set_program_name("Guest Dumpsys FM");
    UpsertStationResponse upsert_resp;
    EXPECT_TRUE(
        guest_stub->UpsertStation(&upsert_ctx, upsert_req, &upsert_resp).ok());
    EXPECT_EQ(upsert_resp.station().program_name(), "Guest Dumpsys FM");

    ::grpc::ClientContext remove_ctx;
    RemoveStationRequest remove_req;
    remove_req.set_band(RadioBand::FM);
    remove_req.set_frequency_hz(89100000);
    RemoveStationResponse remove_resp;
    EXPECT_TRUE(
        guest_stub->RemoveStation(&remove_ctx, remove_req, &remove_resp).ok());
    EXPECT_TRUE(remove_resp.removed());

    // Out-of-range HD subchannel and oversized station metadata are rejected.
    ::grpc::ClientContext bad_hd_ctx;
    UpsertStationRequest bad_hd_req;
    bad_hd_req.mutable_station()->set_band(RadioBand::FM);
    bad_hd_req.mutable_station()->set_frequency_hz(89100000);
    bad_hd_req.mutable_station()->set_hd_subchannel(8);
    UpsertStationResponse bad_hd_resp;
    EXPECT_EQ(guest_stub->UpsertStation(&bad_hd_ctx, bad_hd_req, &bad_hd_resp)
                  .error_code(),
              ::grpc::StatusCode::INVALID_ARGUMENT);

    ::grpc::ClientContext big_meta_ctx;
    UpsertStationRequest big_meta_req;
    big_meta_req.mutable_station()->set_band(RadioBand::FM);
    big_meta_req.mutable_station()->set_frequency_hz(89100000);
    big_meta_req.mutable_station()->set_rds_rt(
        std::string(kMaxStationProgramBytes + 1, 'X'));
    UpsertStationResponse big_meta_resp;
    EXPECT_EQ(
        guest_stub->UpsertStation(&big_meta_ctx, big_meta_req, &big_meta_resp)
            .error_code(),
        ::grpc::StatusCode::INVALID_ARGUMENT);
  }

  // Attempting to invoke VirtualTunerControlService/SetStationAudio against the
  // guest-facing server must fail with UNIMPLEMENTED.
  std::unique_ptr<VirtualTunerControlService::Stub> control_stub_on_guest =
      VirtualTunerControlService::NewStub(
          guest_server->InProcessChannel(::grpc::ChannelArguments()));
  {
    ::grpc::ClientContext audio_ctx;
    SetStationAudioRequest audio_req;
    audio_req.set_band(RadioBand::FM);
    audio_req.set_frequency_hz(96500000);
    audio_req.set_wav_path("/etc/hosts");
    SetStationAudioResponse audio_resp;
    ::grpc::Status status = control_stub_on_guest->SetStationAudio(
        &audio_ctx, audio_req, &audio_resp);
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::UNIMPLEMENTED);
  }

  guest_server->Shutdown();
}

TEST(VirtualTunerServiceTest,
     ControlServiceDuplicatesControlsAndManagesStationAudio) {
  TunerState state;
  VirtualTunerServiceImpl guest_service(state);
  VirtualTunerControlServiceImpl control_service(state);

  ::grpc::ServerBuilder guest_builder;
  guest_builder.RegisterService(&guest_service);
  std::unique_ptr<::grpc::Server> guest_server(guest_builder.BuildAndStart());
  ASSERT_NE(guest_server, nullptr);

  ::grpc::ServerBuilder control_builder;
  control_builder.RegisterService(&control_service);
  std::unique_ptr<::grpc::Server> control_server(
      control_builder.BuildAndStart());
  ASSERT_NE(control_server, nullptr);

  std::unique_ptr<VirtualTuner::Stub> guest_stub = VirtualTuner::NewStub(
      guest_server->InProcessChannel(::grpc::ChannelArguments()));
  std::unique_ptr<VirtualTunerControlService::Stub> control_stub =
      VirtualTunerControlService::NewStub(
          control_server->InProcessChannel(::grpc::ChannelArguments()));

  // Upserting and tuning on the host control service updates shared TunerState
  // visible to the guest service.
  {
    ::grpc::ClientContext upsert_ctx;
    UpsertStationRequest upsert_req;
    upsert_req.mutable_station()->set_band(RadioBand::FM);
    upsert_req.mutable_station()->set_frequency_hz(90900000);
    upsert_req.mutable_station()->set_program_name("Host Control FM");
    UpsertStationResponse upsert_resp;
    ASSERT_TRUE(
        control_stub->UpsertStation(&upsert_ctx, upsert_req, &upsert_resp)
            .ok());

    ::grpc::ClientContext tune_ctx;
    TuneRequest tune_req;
    tune_req.set_band(RadioBand::FM);
    tune_req.set_frequency_hz(90900000);
    TuneResponse tune_resp;
    ASSERT_TRUE(control_stub->Tune(&tune_ctx, tune_req, &tune_resp).ok());

    ::grpc::ClientContext status_ctx;
    GetStatusRequest status_req;
    GetStatusResponse status_resp;
    ASSERT_TRUE(
        guest_stub->GetStatus(&status_ctx, status_req, &status_resp).ok());
    EXPECT_TRUE(status_resp.status().is_playing());
    EXPECT_EQ(status_resp.status().current_station().program_name(),
              "Host Control FM");
  }

  std::string dir_template = "/tmp/cf_virtual_tuner_svc_XXXXXX";
  ASSERT_NE(::mkdtemp(dir_template.data()), nullptr);
  const std::string wav_path = dir_template + "/station.wav";
  const std::string link_path = dir_template + "/link.wav";
  {
    const std::vector<int16_t> samples = {1000, -1000, 2000, -2000};
    const std::vector<uint8_t> wav_bytes = BuildStereoTestWav(samples);
    std::ofstream out(wav_path, std::ios::out | std::ios::binary);
    out.write(reinterpret_cast<const char*>(wav_bytes.data()),
              wav_bytes.size());
  }
  ASSERT_EQ(::symlink(wav_path.c_str(), link_path.c_str()), 0);

  // Valid regular WAV file succeeds on the host control service.
  {
    ::grpc::ClientContext audio_ctx;
    SetStationAudioRequest audio_req;
    audio_req.set_band(RadioBand::FM);
    audio_req.set_frequency_hz(90900000);
    audio_req.set_hd_subchannel(0);
    audio_req.set_wav_path(wav_path);
    SetStationAudioResponse audio_resp;
    ASSERT_TRUE(
        control_stub->SetStationAudio(&audio_ctx, audio_req, &audio_resp).ok());
    EXPECT_EQ(state.GetSnapshot().station_wav_path, wav_path);
  }

  // Symlink and relative paths are rejected with INVALID_ARGUMENT and preserve
  // the existing WAV.
  for (const std::string& bad_path :
       {link_path, std::string("relative/station.wav")}) {
    ::grpc::ClientContext audio_ctx;
    SetStationAudioRequest audio_req;
    audio_req.set_band(RadioBand::FM);
    audio_req.set_frequency_hz(90900000);
    audio_req.set_hd_subchannel(0);
    audio_req.set_wav_path(bad_path);
    SetStationAudioResponse audio_resp;
    ::grpc::Status status =
        control_stub->SetStationAudio(&audio_ctx, audio_req, &audio_resp);
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(state.GetSnapshot().station_wav_path, wav_path);
  }

  // Empty wav_path clears custom station audio.
  {
    ::grpc::ClientContext audio_ctx;
    SetStationAudioRequest audio_req;
    audio_req.set_band(RadioBand::FM);
    audio_req.set_frequency_hz(90900000);
    audio_req.set_hd_subchannel(0);
    audio_req.set_wav_path("");
    SetStationAudioResponse audio_resp;
    ASSERT_TRUE(
        control_stub->SetStationAudio(&audio_ctx, audio_req, &audio_resp).ok());
    EXPECT_TRUE(state.GetSnapshot().station_wav_path.empty());
  }

  ::unlink(link_path.c_str());
  ::unlink(wav_path.c_str());
  ::rmdir(dir_template.c_str());

  control_server->Shutdown();
  guest_server->Shutdown();
}

}  // namespace
}  // namespace virtualtuner
}  // namespace cuttlefish
