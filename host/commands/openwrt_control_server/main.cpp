/*
 *
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <string>

#include <gflags/gflags.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "openwrt_control.grpc.pb.h"

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using openwrtcontrolserver::OpenwrtControlService;
using openwrtcontrolserver::OpenwrtIpaddrReply;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");

DEFINE_bool(bridged_wifi_tap, false,
            "True for using cvd-wtap-XX, false for using cvd-wifiap-XX");
DEFINE_string(launcher_log_path, "", "File path for launcher.log");
DEFINE_string(openwrt_log_path, "", "File path for crosvm_openwrt.log");

namespace cuttlefish {

class OpenwrtControlServiceImpl final : public OpenwrtControlService::Service {
  Status OpenwrtIpaddr(ServerContext* context, const Empty* request,
                       OpenwrtIpaddrReply* response) override {
    // TODO(seungjaeyoo) : Find IP address from crosvm_openwrt.log when using
    // cvd-wtap-XX after disabling DHCP inside OpenWRT in bridged_wifi_tap mode.
    auto result = FindIpaddrLauncherLog();

    response->set_is_error(!TypeIsSuccess(result));
    if (TypeIsSuccess(result)) {
      response->set_ipaddr(*result);
    }
    return Status::OK;
  }

  Result<std::string> FindIpaddrLauncherLog() {
    if (!FileExists(FLAGS_launcher_log_path)) {
      return CF_ERR("launcher.log doesn't exist");
    }

    std::regex re("wan_ipaddr=[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+");
    std::smatch matches;
    std::ifstream ifs(FLAGS_launcher_log_path);
    std::string line, last_match;
    while (std::getline(ifs, line)) {
      if (std::regex_search(line, matches, re)) {
        last_match = matches[0];
      }
    }

    if (last_match.empty()) {
      return CF_ERR("IP address is not found from launcher.log");
    } else {
      return last_match.substr(last_match.find('=') + 1);
    }
  }
};

}  // namespace cuttlefish

void RunServer() {
  std::string server_address("unix:" + FLAGS_grpc_uds_path);
  cuttlefish::OpenwrtControlServiceImpl service;

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

int main(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  RunServer();

  return 0;
}