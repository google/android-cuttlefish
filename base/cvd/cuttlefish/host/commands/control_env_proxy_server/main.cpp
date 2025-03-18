/*
 *
 * Copyright 2023 The Android Open Source Project
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

#include <iostream>
#include <memory>
#include <string>

#include <gflags/gflags.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "control_env_proxy.grpc.pb.h"
#include "host/libs/control_env/grpc_service_handler.h"

using controlenvproxyserver::CallUnaryMethodReply;
using controlenvproxyserver::CallUnaryMethodRequest;
using controlenvproxyserver::ControlEnvProxyService;
using controlenvproxyserver::ListMethodsReply;
using controlenvproxyserver::ListMethodsRequest;
using controlenvproxyserver::ListReqResTypeReply;
using controlenvproxyserver::ListReqResTypeRequest;
using controlenvproxyserver::ListServicesReply;
using controlenvproxyserver::TypeInformationReply;
using controlenvproxyserver::TypeInformationRequest;
using google::protobuf::Empty;
using google::protobuf::RepeatedPtrField;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");
DEFINE_string(grpc_socket_path, "", "The path of gRPC sockets");

class ControlEnvProxyServiceImpl final
    : public ControlEnvProxyService::Service {
 public:
  Status CallUnaryMethod(ServerContext* context,
                         const CallUnaryMethodRequest* request,
                         CallUnaryMethodReply* reply) override {
    std::vector<std::string> args{request->service_name(),
                                  request->method_name(),
                                  request->json_formatted_proto()};
    auto result = cuttlefish::HandleCmds(FLAGS_grpc_socket_path, "call", args);
    if (!TypeIsSuccess(result)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Calling gRPC method failed");
    }
    reply->set_json_formatted_proto(*result);

    return Status::OK;
  }

  Status ListServices(ServerContext* context, const Empty* request,
                      ListServicesReply* reply) override {
    std::vector<std::string> args;
    auto result = cuttlefish::HandleCmds(FLAGS_grpc_socket_path, "ls", args);
    if (!TypeIsSuccess(result)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Listing gRPC services failed");
    }

    Json::Value value;
    if (!reader.parse(*result, value)) {
      return parsing_json_failure_status;
    }
    if (!value["services"].isArray()) {
      return parsing_json_failure_status;
    }
    for (auto& service : value["services"]) {
      if (!service.isString()) {
        return parsing_json_failure_status;
      }
      reply->add_services(service.asString());
    }

    return Status::OK;
  }

  Status ListMethods(ServerContext* context, const ListMethodsRequest* request,
                     ListMethodsReply* reply) override {
    std::vector<std::string> args{request->service_name()};
    auto result = cuttlefish::HandleCmds(FLAGS_grpc_socket_path, "ls", args);
    if (!TypeIsSuccess(result)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Listing gRPC methods failed");
    }

    Json::Value value;
    if (!reader.parse(*result, value)) {
      return parsing_json_failure_status;
    }
    if (!value["methods"].isArray()) {
      return parsing_json_failure_status;
    }
    for (auto& method : value["methods"]) {
      if (!method.isString()) {
        return parsing_json_failure_status;
      }
      reply->add_methods(method.asString());
    }

    return Status::OK;
  }

  Status ListReqResType(ServerContext* context,
                        const ListReqResTypeRequest* request,
                        ListReqResTypeReply* reply) override {
    std::vector<std::string> args{request->service_name(),
                                  request->method_name()};
    auto result = cuttlefish::HandleCmds(FLAGS_grpc_socket_path, "ls", args);
    if (!TypeIsSuccess(result)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Listing gRPC request and response message type failed");
    }

    Json::Value value;
    if (!reader.parse(*result, value)) {
      return parsing_json_failure_status;
    }
    if (!value["request_type"].isString() ||
        !value["response_type"].isString()) {
      return parsing_json_failure_status;
    }
    reply->set_request_type_name(value["request_type"].asString());
    reply->set_response_type_name(value["response_type"].asString());

    return Status::OK;
  }

  Status TypeInformation(ServerContext* context,
                         const TypeInformationRequest* request,
                         TypeInformationReply* reply) override {
    std::vector<std::string> args{request->service_name(),
                                  request->type_name()};
    auto result = cuttlefish::HandleCmds(FLAGS_grpc_socket_path, "type", args);
    if (!TypeIsSuccess(result)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Calling gRPC method failed");
    }
    reply->set_text_formatted_type_info(*result);

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

  Json::Reader reader;
  Status parsing_json_failure_status = Status(
      StatusCode::FAILED_PRECONDITION, "Parsing result into json failed");
};

void RunServer() {
  std::string server_address("unix:" + FLAGS_grpc_uds_path);
  ControlEnvProxyServiceImpl service;

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

  // Let the socket for this server as writable
  auto change_group_result =
      cuttlefish::ChangeGroup(FLAGS_grpc_uds_path, "cvdnetwork");
  if (!TypeIsSuccess(change_group_result)) {
    std::cout << "Failed ChangeGroup " << FLAGS_grpc_uds_path << std::endl;
  }
  int chmod_result = chmod(FLAGS_grpc_uds_path.c_str(), 0775);
  if (chmod_result) {
    std::cout << "Failed chmod 775 " << FLAGS_grpc_uds_path << std::endl;
  }

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  RunServer();

  return 0;
}