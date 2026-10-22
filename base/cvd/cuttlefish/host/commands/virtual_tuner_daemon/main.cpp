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

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/flag_parser/shared_fd_flag.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/pcm_stream_server.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_service.h"
#include "cuttlefish/host/libs/config/logging.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

struct DaemonFlags {
  std::string server_address;
  SharedFD pcm_server_fd;
  size_t pcm_channels = kChannels;
};

Result<DaemonFlags> GetFlagValues(int argc, char** argv) {
  DaemonFlags flag_values;
  std::vector<Flag> flags;
  flags.emplace_back(
      GflagsCompatFlag("server_address", flag_values.server_address)
          .Help("gRPC listening server address (e.g. "
                "unix:/tmp/vsock_3_1000/vm.vsock_7010)"));
  // run_cvd creates the listening socket before starting WebRTC, which
  // connects to it only once.
  flags.emplace_back(
      SharedFDFlag("pcm_server_fd", flag_values.pcm_server_fd)
          .Help("File descriptor of the PCM stream listening socket"));
  flags.emplace_back(
      GflagsCompatFlag("pcm_channels", flag_values.pcm_channels)
          .Help("Channel count of the guest's virtual_tuner audio stream"));
  flags.emplace_back(HelpFlag(flags));

  std::vector<std::string> args(argv + 1, argv + argc);
  CF_EXPECT(ConsumeFlags(flags, args, {.fail_on_unexpected_argument = true}),
            "Could not process command line flags");
  CF_EXPECT(!flag_values.server_address.empty(),
            "Did not receive a --server_address");
  CF_EXPECT(flag_values.pcm_server_fd->IsOpen(),
            "Did not receive a valid --pcm_server_fd");
  CF_EXPECTF(flag_values.pcm_channels == kChannels,
             "Unsupported --pcm_channels={}: the daemon only generates "
             "{}-channel audio",
             flag_values.pcm_channels, kChannels);
  return flag_values;
}

// Builds and starts the gRPC control server on `server_address`. `service`
// must outlive the returned server.
Result<std::unique_ptr<::grpc::Server>> StartControlServer(
    const std::string& server_address, VirtualTunerServiceImpl& service) {
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;

  builder.RegisterService(&service);
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());

  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  CF_EXPECTF(server != nullptr, "Failed to start gRPC Server on {}",
             server_address);
  return server;
}

}  // namespace

Result<void> VirtualTunerDaemonMain(int argc, char** argv) {
  DefaultSubprocessLogging(argv);

  DaemonFlags flag_values = CF_EXPECT(GetFlagValues(argc, argv));

  VLOG(0) << "Virtual Tuner gRPC Control Server starting on address: "
          << flag_values.server_address;

  TunerState tuner_state;

  // Start the PCM audio stream server
  PcmStreamServer pcm_server(tuner_state, flag_values.pcm_server_fd);
  CF_EXPECT(pcm_server.Start());

  VirtualTunerServiceImpl service(tuner_state);
  std::unique_ptr<::grpc::Server> server =
      CF_EXPECT(StartControlServer(flag_values.server_address, service));

  VLOG(0) << "Virtual Tuner Daemon is fully initialized and running.";
  server->Wait();
  return {};
}

}  // namespace virtualtuner
}  // namespace cuttlefish

int main(int argc, char** argv) {
  cuttlefish::Result<void> result =
      cuttlefish::virtualtuner::VirtualTunerDaemonMain(argc, argv);
  if (!result.has_value()) {
    LOG(ERROR) << result.error();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
