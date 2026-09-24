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

#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/flag_parser/shared_fd_flag.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/pcm_stream_server.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_service.h"
#include "cuttlefish/host/libs/config/logging.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace virtualtuner {

int VirtualTunerDaemonMain(int argc, char** argv) {
  DefaultSubprocessLogging(argv);

  std::vector<Flag> flags;
  std::string server_address;
  SharedFD pcm_server_fd;

  flags.emplace_back(GflagsCompatFlag("server_address", server_address)
                         .Help("gRPC listening server address (e.g. "
                               "unix:/tmp/vsock_3_1000/vm.vsock_7010)"));
  flags.emplace_back(
      SharedFDFlag("pcm_server_fd", pcm_server_fd)
          .Help("File descriptor to an already created PCM stream server "
                "socket"));
  flags.emplace_back(HelpFlag(flags));

  std::vector<std::string> args(argv + 1, argv + argc);
  Result<void> parse_res =
      ConsumeFlags(flags, args, {.fail_on_unexpected_argument = true});
  if (!parse_res.has_value()) {
    LOG(FATAL) << "Could not process command line flags: "
               << parse_res.error().FormatForEnv();
  }

  if (server_address.empty()) {
    LOG(FATAL) << "Did not receive a --server_address";
  }

  if (!pcm_server_fd->IsOpen()) {
    LOG(FATAL) << "Did not receive a valid --pcm_server_fd";
  }

  LOG(INFO) << "Virtual Tuner gRPC Control Server starting on address: "
            << server_address;

  TunerState tuner_state;

  // Start the PCM audio stream server
  PcmStreamServer pcm_server(tuner_state, pcm_server_fd);
  Result<void> pcm_res = pcm_server.Start();
  if (!pcm_res.has_value()) {
    LOG(FATAL) << "Failed to start PCM streaming server: "
               << pcm_res.error().FormatForEnv();
  }

  // Start the gRPC control server
  VirtualTunerServiceImpl service(tuner_state);
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;

  builder.RegisterService(&service);
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());

  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    LOG(FATAL) << "Failed to start gRPC Server on " << server_address;
  }

  LOG(INFO) << "Virtual Tuner Daemon is fully initialized and running.";
  server->Wait();

  pcm_server.Stop();
  return 0;
}

}  // namespace virtualtuner
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::virtualtuner::VirtualTunerDaemonMain(argc, argv);
}
