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

#include <memory>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.grpc.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

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
    request.set_frequency_hz(101100000);
    request.set_hd_subchannel(0);

    TuneResponse response;
    ::grpc::Status status = stub->Tune(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(response.success());
    EXPECT_EQ(state.GetFrequency(), 101100000u);
    EXPECT_TRUE(state.IsPlaying());
  }

  // 2. Stop RPC
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

}  // namespace
}  // namespace virtualtuner
}  // namespace cuttlefish
