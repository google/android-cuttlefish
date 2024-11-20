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

#include <android-base/hex.h>
#include <gflags/gflags.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "casimir_control.grpc.pb.h"
#include "casimir_controller.h"
#include "utils.h"

#include "common/libs/utils/result.h"

using casimircontrolserver::CasimirControlService;
using casimircontrolserver::PowerLevel;
using casimircontrolserver::RadioState;
using casimircontrolserver::SendApduReply;
using casimircontrolserver::SendApduRequest;
using casimircontrolserver::SenderId;
using casimircontrolserver::Void;

using cuttlefish::CasimirController;

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using std::string;
using std::vector;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");
DEFINE_int32(casimir_rf_port, -1, "RF port to control Casimir");
DEFINE_string(casimir_rf_path, "", "RF unix server path to control Casimir");

#define CHECK_RETURN(call, msg)                          \
  auto res = call;                                       \
  if (!res.ok()) {                                       \
    LOG(ERROR) << msg;                                   \
    return Status(StatusCode::FAILED_PRECONDITION, msg); \
  }
#define ENSURE_INIT()        \
  {                          \
    const auto res = Init(); \
    if (!res.ok()) {         \
      return res;            \
    }                        \
  }

namespace cuttlefish {
namespace {

class CasimirControlServiceImpl final : public CasimirControlService::Service {
  CasimirController device;
  bool isInitialized = false;
  bool isRadioOn = false;

  Status SetPowerLevel(ServerContext* context, const PowerLevel* power_level,
                       Void*) override {
    if (!isInitialized) {
      return Status::OK;
    }
    if (!device.SetPowerLevel(power_level->power_level())) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to set power level");
    }
    return Status::OK;
  }

  Status Init(ServerContext*, const Void*, Void*) override { return Init(); }

  Status Init() {
    if (isInitialized) {
      return Status::OK;
    }
    // Step 1: Initialize connection with casimir
    cuttlefish::Result<void> init_res;
    if (FLAGS_casimir_rf_port >= 0) {
      init_res = device.Init(FLAGS_casimir_rf_port);
    } else if (!FLAGS_casimir_rf_path.empty()) {
      init_res = device.Init(FLAGS_casimir_rf_path);
    } else {
      std::string message =
          "`--casimir_rf_port` or `--casimir_rf_path` must be set";
      LOG(ERROR) << message;
      return Status(StatusCode::FAILED_PRECONDITION, message);
    }
    if (!init_res.ok()) {
      LOG(ERROR) << "Failed to initialize connection to casimir: "
                 << init_res.error().FormatForEnv();
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to connect with casimir");
    }
    isInitialized = true;
    return Status::OK;
  }

  Status Close(ServerContext* context, const Void*, Void*) override {
    if (!isInitialized) {
      return Status::OK;
    }
    isInitialized = false;
    CHECK_RETURN(device.Close(), "Failed to close connection to casimir")
    return Status::OK;
  }

  Status Mute() {
    if (isRadioOn) {
      CHECK_RETURN(device.Mute(), "Failed to mute radio")
      isRadioOn = false;
    }
    return Status::OK;
  }

  Status Unmute() {
    if (!isRadioOn) {
      CHECK_RETURN(device.Unmute(), "Failed to unmute radio")
      isRadioOn = true;
    }
    return Status::OK;
  }

  Status SetRadioState(ServerContext* context, const RadioState* radio_state,
                       Void*) override {
    if (radio_state->radio_on()) {
      ENSURE_INIT()
      return Unmute();
    } else {
      if (!isInitialized) {
        return Status::OK;
      }
      return Mute();
    }
  }

  Status PollA(ServerContext* context, const Void*,
               SenderId* senderId) override {
    ENSURE_INIT()
    if (!isInitialized) {
      // Step 1: Initialize connection with casimir
      cuttlefish::Result<void> init_res;
      if (FLAGS_casimir_rf_port >= 0) {
        init_res = device.Init(FLAGS_casimir_rf_port);
      } else if (!FLAGS_casimir_rf_path.empty()) {
        init_res = device.Init(FLAGS_casimir_rf_path);
      } else {
        std::string message =
            "`--casimir_rf_port` or `--casimir_rf_path` must be set";
        LOG(ERROR) << message;
        return Status(StatusCode::FAILED_PRECONDITION, message);
      }
      if (!init_res.ok()) {
        LOG(ERROR) << "Failed to initialize connection to casimir: "
                   << init_res.error().FormatForEnv();
        return Status(StatusCode::FAILED_PRECONDITION,
                      "Failed to connect with casimir");
      }
      isInitialized = true;
      CHECK_RETURN(Unmute(), "failed to unmute the device")
    }
    // Step 2: Poll
    auto poll_res = device.Poll();
    if (!poll_res.ok()) {
      LOG(ERROR) << "Failed to poll(): " << poll_res.error().FormatForEnv();
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to poll and select NFC-A and ISO-DEP");
    }
    uint32_t id = static_cast<uint32_t>(poll_res.value());
    senderId->set_sender_id(id);
    return Status::OK;
  }

  Status SendApdu(ServerContext* context, const SendApduRequest* request,
                  SendApduReply* response) override {
    // Step 0: Parse input
    std::vector<std::shared_ptr<std::vector<uint8_t>>> apdu_bytes;
    for (int i = 0; i < request->apdu_hex_strings_size(); i++) {
      auto apdu_bytes_res = BytesArray(request->apdu_hex_strings(i));
      if (!apdu_bytes_res.ok()) {
        LOG(ERROR) << "Failed to parse input " << request->apdu_hex_strings(i)
                   << ", " << apdu_bytes_res.error().FormatForEnv();
        return Status(StatusCode::INVALID_ARGUMENT,
                      "Failed to parse input. Must only contain [0-9a-fA-F]");
      }
      apdu_bytes.push_back(apdu_bytes_res.value());
    }
    ENSURE_INIT()

    int16_t id;
    if (request->has_sender_id()) {
      id = request->sender_id();
    } else {
      // Step 2: Poll
      Void voidArg;
      SenderId senderId;
      PollA(context, &voidArg, &senderId);
      id = senderId.sender_id();
    }

    // Step 3: Send APDU bytes
    response->clear_response_hex_strings();
    for (int i = 0; i < apdu_bytes.size(); i++) {
      auto send_res = device.SendApdu(id, apdu_bytes[i]);
      if (!send_res.ok()) {
        LOG(ERROR) << "Failed to send APDU bytes: "
                   << send_res.error().FormatForEnv();
        return Status(StatusCode::UNKNOWN, "Failed to send APDU bytes");
      }
      auto bytes = *(send_res.value());
      auto resp = android::base::HexString(
          reinterpret_cast<void*>(bytes.data()), bytes.size());
      response->add_response_hex_strings(resp);
    }

    // Returns OK although returned bytes is valids if ends with [0x90, 0x00].
    return Status::OK;
  }
};

void RunServer(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::string server_address("unix:" + FLAGS_grpc_uds_path);
  CasimirControlServiceImpl service;

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
  cuttlefish::RunServer(argc, argv);

  return 0;
}
