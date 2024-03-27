/*
 * Copyright 2023 The Android Open Source Project
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

#include <iostream>
#include <memory>
#include <string>

#include <gflags/gflags.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"
#include "run_cvd.pb.h"
#include "screen_recording.grpc.pb.h"

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using screenrecordingserver::ScreenRecordingService;
using screenrecordingserver::StartRecordingResponse;
using screenrecordingserver::StopRecordingResponse;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");

const int COMMAND_TIMEOUT_SEC = 10;

namespace cuttlefish {
namespace {

class ScreenRecordingServiceImpl final
    : public ScreenRecordingService::Service {
  Status StartRecording(ServerContext* context, const Empty* request,
                        StartRecordingResponse* reply) override {
    run_cvd::ExtendedLauncherAction start_action;
    start_action.mutable_start_screen_recording();

    std::vector<bool> successes = SendToAllInstances(start_action);
    reply->mutable_successes()->Assign(successes.begin(), successes.end());
    return Status::OK;
  }

  Status StopRecording(ServerContext* context, const Empty* request,
                       StopRecordingResponse* reply) override {
    run_cvd::ExtendedLauncherAction stop_action;
    stop_action.mutable_stop_screen_recording();

    std::vector<bool> successes = SendToAllInstances(stop_action);
    reply->mutable_successes()->Assign(successes.begin(), successes.end());
    return Status::OK;
  }

 private:
  std::vector<bool> SendToAllInstances(
      run_cvd::ExtendedLauncherAction extended_action) {
    std::vector<bool> successes;

    auto launcher_monitor_sockets = GetLauncherMonitorSockets();
    for (const SharedFD& socket : *launcher_monitor_sockets) {
      Result<void> result =
          RunLauncherAction(socket, extended_action, std::nullopt);
      successes.push_back(result.ok());
    }

    return successes;
  }

  Result<std::vector<SharedFD>> GetLauncherMonitorSockets() {
    std::vector<SharedFD> monitor_sockets;

    const CuttlefishConfig* config =
        CF_EXPECT(CuttlefishConfig::Get(), "Failed to obtain config object");
    std::vector<CuttlefishConfig::InstanceSpecific> instance_specifics =
        config->Instances();
    for (const CuttlefishConfig::InstanceSpecific& instance_specific :
         instance_specifics) {
      auto monitor_socket = CF_EXPECT(GetLauncherMonitorFromInstance(
          instance_specific, COMMAND_TIMEOUT_SEC));
      monitor_sockets.push_back(monitor_socket);
    }

    return monitor_sockets;
  }
};

void RunScreenRecordingServer() {
  std::string server_address("unix:" + FLAGS_grpc_uds_path);
  ScreenRecordingServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  cuttlefish::RunScreenRecordingServer();

  return 0;
}
