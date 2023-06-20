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

#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/libs/web/http_client/http_client.h"
#include "openwrt_control.grpc.pb.h"

using android::base::StartsWith;
using google::protobuf::Empty;
using google::protobuf::RepeatedPtrField;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using openwrtcontrolserver::LuciRpcReply;
using openwrtcontrolserver::LuciRpcRequest;
using openwrtcontrolserver::OpenwrtControlService;
using openwrtcontrolserver::OpenwrtIpaddrReply;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");
DEFINE_bool(bridged_wifi_tap, false,
            "True for using cvd-wtap-XX, false for using cvd-wifiap-XX");
DEFINE_string(webrtc_device_id, "", "The device ID in WebRTC like cvd-1");
DEFINE_string(launcher_log_path, "", "File path for launcher.log");
DEFINE_string(openwrt_log_path, "", "File path for crosvm_openwrt.log");

constexpr char kErrorMessageRpc[] = "Luci RPC request failed";
constexpr char kErrorMessageRpcAuth[] = "Luci authentication request failed";

namespace cuttlefish {

class OpenwrtControlServiceImpl final : public OpenwrtControlService::Service {
 public:
  OpenwrtControlServiceImpl(HttpClient& http_client)
      : http_client_(http_client) {}

  Status LuciRpc(ServerContext* context, const LuciRpcRequest* request,
                 LuciRpcReply* response) override {
    // Update authentication key when it's empty.
    if (auth_key_.empty()) {
      if (!TypeIsSuccess(UpdateLuciRpcAuthKey())) {
        return Status(StatusCode::UNAVAILABLE, kErrorMessageRpcAuth);
      }
    }

    auto reply = RequestLuciRpc(request->subpath(), request->method(),
                                ToVector(request->params()));

    // When RPC request fails, update authentication key and retry once.
    if (!TypeIsSuccess(reply)) {
      if (!TypeIsSuccess(UpdateLuciRpcAuthKey())) {
        return Status(StatusCode::UNAVAILABLE, kErrorMessageRpcAuth);
      }
      reply = RequestLuciRpc(request->subpath(), request->method(),
                             ToVector(request->params()));
      if (!TypeIsSuccess(reply)) {
        return Status(StatusCode::UNAVAILABLE, kErrorMessageRpc);
      }
    }

    Json::FastWriter writer;
    response->set_id((*reply)["id"].asInt());
    response->set_error((*reply)["error"].asString());
    response->set_result(writer.write((*reply)["result"]));

    return Status::OK;
  }

  Status OpenwrtIpaddr(ServerContext* context, const Empty* request,
                       OpenwrtIpaddrReply* response) override {
    // TODO(seungjaeyoo) : Find IP address from crosvm_openwrt.log when using
    // cvd-wtap-XX after disabling DHCP inside OpenWRT in bridged_wifi_tap mode.
    auto ipaddr = FindIpaddrLauncherLog();
    if (!TypeIsSuccess(ipaddr)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to get Openwrt IP address");
    }
    response->set_ipaddr(*ipaddr);
    return Status::OK;
  }

 private:
  template <typename T>
  std::vector<T> ToVector(const RepeatedPtrField<T>& repeated_field) {
    std::vector<T> vec;
    for (const auto& value : repeated_field) {
      vec.push_back(value);
    }
    return vec;
  }

  Result<std::string> LuciRpcAddress(const std::string& subpath) {
    auto ipaddr = CF_EXPECT(FindIpaddrLauncherLog());
    return "http://" + ipaddr + "/devices/" + FLAGS_webrtc_device_id +
           "/openwrt/cgi-bin/luci/rpc/" + subpath;
  }

  Result<std::string> LuciRpcAddress(const std::string& subpath,
                                     const std::string& auth_key) {
    auto addr_without_auth = CF_EXPECT(LuciRpcAddress(subpath));
    return addr_without_auth + "?auth=" + auth_key;
  }

  Json::Value LuciRpcData(const std::string& method,
                          const std::vector<std::string>& params) {
    Json::Value data;
    data["method"] = method;
    Json::Value params_json_obj(Json::arrayValue);
    for (const auto& param : params) {
      params_json_obj.append(param);
    }
    data["params"] = params_json_obj;
    return data;
  }

  Json::Value LuciRpcData(int id, const std::string& method,
                          const std::vector<std::string>& params) {
    Json::Value data = LuciRpcData(method, params);
    data["id"] = id;
    return data;
  }

  Result<void> UpdateLuciRpcAuthKey() {
    auto auth_url = CF_EXPECT(LuciRpcAddress("auth"));
    auto auth_data = LuciRpcData(1, "login", {"root", "password"});
    auto auth_reply =
        CF_EXPECT(http_client_.PostToJson(auth_url, auth_data, header_));
    if (auth_reply.data["error"].isString()) {
      CF_EXPECT(!StartsWith(auth_reply.data["error"].asString(),
                            "Failed to parse json:"),
                kErrorMessageRpcAuth);
    }
    CF_EXPECT(auth_reply.data["result"].isString(),
              "Reply doesn't contain result");
    auth_key_ = auth_reply.data["result"].asString();

    return {};
  }

  Result<Json::Value> RequestLuciRpc(const std::string& subpath,
                                     const std::string& method,
                                     const std::vector<std::string>& params) {
    auto url = CF_EXPECT(LuciRpcAddress(subpath, auth_key_));
    auto data = LuciRpcData(method, params);
    auto reply = CF_EXPECT(http_client_.PostToJson(url, data, header_));
    if (reply.data["error"].isString()) {
      CF_EXPECT(
          !StartsWith(reply.data["error"].asString(), "Failed to parse json:"),
          kErrorMessageRpc);
    }
    return reply.data;
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

  HttpClient& http_client_;
  const std::vector<std::string> header_{"Content-Type: application/json"};
  std::string auth_key_;
};

}  // namespace cuttlefish

void RunServer() {
  std::string server_address("unix:" + FLAGS_grpc_uds_path);
  auto http_client = cuttlefish::HttpClient::CurlClient();
  cuttlefish::OpenwrtControlServiceImpl service(*http_client);

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