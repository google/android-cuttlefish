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
#include <vector>

#include <android-base/hex.h>
#include <gflags/gflags.h>
#include <google/protobuf/empty.pb.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include "absl/log/log.h"

#include "cuttlefish/host/commands/casimir_control_server/casimir_control.grpc.pb.h"
#include "cuttlefish/host/commands/casimir_control_server/casimir_controller.h"
#include "cuttlefish/host/commands/casimir_control_server/hex.h"
#include "cuttlefish/result/result.h"

using casimircontrolserver::CasimirControlService;
using casimircontrolserver::PowerLevel;
using casimircontrolserver::RadioState;
using casimircontrolserver::SendApduReply;
using casimircontrolserver::SendApduRequest;
using casimircontrolserver::SendBroadcastRequest;
using casimircontrolserver::SendBroadcastResponse;
using casimircontrolserver::SenderId;
using casimircontrolserver::TransceiveConfiguration;
using casimircontrolserver::Void;

using cuttlefish::CasimirController;

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");
DEFINE_int32(casimir_rf_port, -1, "RF port to control Casimir");
DEFINE_string(casimir_rf_path, "", "RF unix server path to control Casimir");

namespace cuttlefish {
namespace {

Result<CasimirController> ConnectToCasimir() {
  if (FLAGS_casimir_rf_port >= 0) {
    return CF_EXPECT(
        CasimirController::ConnectToTcpPort(FLAGS_casimir_rf_port));
  } else if (!FLAGS_casimir_rf_path.empty()) {
    return CF_EXPECT(
        CasimirController::ConnectToUnixSocket(FLAGS_casimir_rf_path));
  } else {
    return CF_ERR("`--casimir_rf_port` or `--casimir_rf_path` must be set");
  }
}

Status ResultToStatus(Result<void> res) {
  if (res.ok()) {
    return Status::OK;
  } else {
    LOG(ERROR) << "RPC failed: " << res.error();
    return Status(StatusCode::INTERNAL,
                  res.error().FormatForEnv(/* color = */ false));
  }
}

class CasimirControlServiceImpl final : public CasimirControlService::Service {
 private:
  Status SetPowerLevel(ServerContext* context, const PowerLevel* power_level,
                       Void*) override {
    return ResultToStatus(SetPowerLevelResult(power_level));
  }

  Result<void> SetPowerLevelResult(const PowerLevel* power_level) {
    if (!device_) {
      return {};
    }
    CF_EXPECT(device_->SetPowerLevel(power_level->power_level()),
              "Failed to set power level");
    return {};
  }

  Status Close(ServerContext* context, const Void*, Void* senderId) override {
    device_ = std::nullopt;
    return Status::OK;
  }

  Status Init(ServerContext*, const Void*, Void*) override {
    return ResultToStatus(Init());
  }

  Result<void> Init() {
    if (device_.has_value()) {
      return {};
    }
    // Step 1: Initialize connection with casimir
    device_ = CF_EXPECT(ConnectToCasimir());
    return {};
  }

  Result<void> Mute() {
    if (!device_.has_value()) {
      return {};
    }

    if (is_radio_on_) {
      CF_EXPECT(device_->Mute(), "Failed to mute radio");
      is_radio_on_ = false;
    }
    return {};
  }

  Result<void> Unmute() {
    if (!is_radio_on_) {
      CF_EXPECT(device_->Unmute(), "Failed to unmute radio");
      is_radio_on_ = true;
    }
    return {};
  }

  Status SetRadioState(ServerContext* context, const RadioState* radio_state,
                       Void*) override {
    return ResultToStatus(SetRadioStateResult(radio_state));
  }

  Result<void> SetRadioStateResult(const RadioState* radio_state) {
    if (radio_state->radio_on()) {
      CF_EXPECT(Init());
      CF_EXPECT(Unmute());
      return {};
    } else {
      if (!device_.has_value()) {
        return {};
      }
      CF_EXPECT(Mute());
      return {};
    }
  }

  Result<void> PollAResult(SenderId* sender_id) {
    // Step 1: Initialize connection with casimir
    if (!device_.has_value()) {
      device_ = CF_EXPECT(ConnectToCasimir(), "Failed to connect with casimir");
      CF_EXPECT(Unmute(), "failed to unmute the device");
    }
    // Step 2: Poll
    /* Casimir control server seems to be dropping integer values of zero.
      This works around that issue by translating the 0-based sender IDs to
      be 1-based.*/
    sender_id->set_sender_id(

        CF_EXPECT(device_->Poll(),
                  "Failed to poll and select NFC-A and ISO-DEP") +
        1);
    return {};
  }

  Status PollA(ServerContext*, const Void*, SenderId* sender_id) override {
    return ResultToStatus(PollAResult(sender_id));
  }

  Result<void> SendApduResult(const SendApduRequest* request,
                              SendApduReply* response) {
    // Step 0: Parse input
    std::vector<std::vector<uint8_t>> apdu_bytes;
    for (const std::string& apdu_hex_string : request->apdu_hex_strings()) {
      apdu_bytes.emplace_back(
          CF_EXPECT(HexToBytes(apdu_hex_string),
                    "Failed to parse input. Must only contain [0-9a-fA-F]"));
    }
    // Step 1: Initialize connection with casimir
    CF_EXPECT(Init());

    int16_t id;
    if (request->has_sender_id()) {
      /* Casimir control server seems to be dropping integer values of zero.
        This works around that issue by translating the 0-based sender IDs to
        be 1-based.*/
      id = request->sender_id() - 1;
    } else {
      // Step 2: Poll
      SenderId sender_id;
      CF_EXPECT(PollAResult(&sender_id));
      id = sender_id.sender_id() - 1;
    }

    // Step 3: Send APDU bytes
    response->clear_response_hex_strings();
    for (int i = 0; i < apdu_bytes.size(); i++) {
      std::vector<uint8_t> bytes =
          CF_EXPECT(device_->SendApdu(id, std::move(apdu_bytes[i])),
                    "Failed to send APDU bytes");
      std::string resp = android::base::HexString(
          reinterpret_cast<void*>(bytes.data()), bytes.size());
      response->add_response_hex_strings(std::move(resp));
    }

    // Returns OK although returned bytes is valids if ends with [0x90, 0x00].
    return {};
  }

  Status SendApdu(ServerContext*, const SendApduRequest* request,
                  SendApduReply* response) override {
    return ResultToStatus(SendApduResult(request, response));
  }

  Result<void> SendBroadcastResult(const SendBroadcastRequest* request,
                                   SendBroadcastResponse* response) {
    // Default configuration values
    TransceiveConfiguration requestConfig;
    // Type A
    requestConfig.set_type("A");
    // CRC present
    requestConfig.set_crc(true);
    // 8 bits in last byte
    requestConfig.set_bits(8);
    // 106kbps
    requestConfig.set_bitrate(106);
    // No timeout, timeout immediately
    requestConfig.clear_timeout();
    // 100% output power
    requestConfig.set_power(100);

    // Overwrite defaults with provided configuration, if present
    if (request->has_configuration()) {
      auto config = request->configuration();
      if (config.has_type()) {
        requestConfig.set_type(config.type());
      }
      if (config.has_crc()) {
        requestConfig.set_crc(config.crc());
      }
      if (config.has_bits()) {
        requestConfig.set_bits(config.bits());
      }
      if (config.has_bitrate()) {
        requestConfig.set_bitrate(config.bitrate());
      }
      if (config.has_timeout()) {
        requestConfig.set_timeout(config.timeout());
      }
      if (config.has_power()) {
        requestConfig.set_power(config.power());
      }
    }

    if (!device_.has_value()) {
      device_ = CF_EXPECT(ConnectToCasimir(), "Failed to connect with casimir");
      CF_EXPECT(Unmute(), "failed to unmute the device");
    }

    std::vector<uint8_t> requestData =
        CF_EXPECT(HexToBytes(request->data()),
                  "Failed to parse input. Must only contain [0-9a-fA-F]");

    CF_EXPECT(device_->SendBroadcast(
                  requestData, requestConfig.type(), requestConfig.crc(),
                  requestConfig.bits(), requestConfig.bitrate(),
                  requestConfig.timeout(), requestConfig.power()),
              "Failed to send broadcast data");

    return {};  // Success
  }

  Status SendBroadcast(ServerContext*, const SendBroadcastRequest* request,
                       SendBroadcastResponse* response) override {
    return ResultToStatus(SendBroadcastResult(request, response));
  }

  std::optional<CasimirController> device_;
  bool is_radio_on_ = false;
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
