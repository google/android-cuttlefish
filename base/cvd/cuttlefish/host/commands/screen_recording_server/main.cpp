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

#include <functional>
#include <memory>
#include <string>

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <google/protobuf/empty.pb.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/support/status_code_enum.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/screen_recording_server/screen_recording.grpc.pb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/logging.h"
#include "cuttlefish/host/libs/screen_recording_controls/screen_recording_controls.h"

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using screenrecordingserver::ScreenRecordingService;
using screenrecordingserver::StartRecordingResponse;
using screenrecordingserver::StopRecordingResponse;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");

const int COMMAND_TIMEOUT_SEC = 10;

namespace cuttlefish {
namespace {

using StartStopFn = std::function<Result<void>(
    const CuttlefishConfig::InstanceSpecific&, std::chrono::seconds)>;

class ScreenRecordingServiceImpl final
    : public ScreenRecordingService::Service {
  Status StartRecording(ServerContext* context, const Empty* request,
                        StartRecordingResponse* reply) override {
    return Handle(reply, StartScreenRecording);
  }

  Status StopRecording(ServerContext* context, const Empty* request,
                       StopRecordingResponse* reply) override {
    return Handle(reply, StopScreenRecording);
  }

 private:
  template <typename R>
  Status Handle(R* reply, StartStopFn fn) {
    Result<std::vector<bool>> successes_res = OnAllInstances(fn);
    if (successes_res.ok()) {
      reply->mutable_successes()->Assign(successes_res->begin(),
                                         successes_res->end());
      return Status::OK;
    } else {
      LOG(ERROR) << "Failed to start recording: "
                 << successes_res.error().FormatForEnv();
      reply->mutable_successes()->Add(false);
      return Status(StatusCode::ABORTED,
                    successes_res.error().FormatForEnv(false));
    }
  }

  Result<std::vector<bool>> OnAllInstances(StartStopFn fn) {
    std::vector<bool> successes;
    const CuttlefishConfig* config = CF_EXPECT(CuttlefishConfig::Get());
    for (const auto& instance : config->Instances()) {
      Result<void> result =
          fn(instance, std::chrono::seconds(COMMAND_TIMEOUT_SEC));
      successes.push_back(result.ok());
      if (!result.ok()) {
        LOG(ERROR) << "Failed to communicate with instance " << instance.id()
                   << ": " << result.error().FormatForEnv();
      }
    }
    return successes;
  }
};

void RunScreenRecordingServer(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  DefaultSubprocessLogging(argv);

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
  LOG(DEBUG) << "Server listening on " << server_address;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  cuttlefish::RunScreenRecordingServer(argc, argv);

  return 0;
}
