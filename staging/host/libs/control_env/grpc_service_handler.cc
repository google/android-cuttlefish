/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/strings.h>
#include <grpcpp/security/credentials.h>
#include <json/json.h>
#include <test/cpp/util/cli_credentials.h>
#include <test/cpp/util/grpc_tool.h>
#include <test/cpp/util/test_config.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"

using android::base::EndsWith;
using android::base::Split;
using android::base::Trim;
using grpc::InsecureChannelCredentials;

namespace cuttlefish {
namespace {

constexpr char kDefaultOptionL[] = "-l=false";
constexpr char kDefaultOptionJsonInput[] = "--json_input=true";
constexpr char kDefaultOptionJsonOutput[] = "--json_output=true";
constexpr char kServiceServerReflection[] =
    "grpc.reflection.v1alpha.ServerReflection";
constexpr char kServiceHealth[] = "grpc.health.v1.Health";
constexpr char kServiceControlEnvProxy[] = "ControlEnvProxyService";
constexpr char kServiceControlEnvProxyFull[] =
    "controlenvproxyserver.ControlEnvProxyService";

bool PrintStream(std::stringstream* ss, const std::string& output) {
  (*ss) << output;
  return true;
}

class InsecureCliCredentials final : public grpc::testing::CliCredentials {
 public:
  std::shared_ptr<grpc::ChannelCredentials> GetChannelCredentials()
      const override {
    return InsecureChannelCredentials();
  }
  std::string GetCredentialUsage() const override { return ""; }
};

std::vector<char*> CombineArgumentsAndOptions(
    const std::vector<std::string>& arguments,
    const std::vector<std::string>& options) {
  std::vector<char*> char_vec;
  // Add 3 for default options
  char_vec.reserve(arguments.size() + options.size() + 3);
  for (const auto& arg : arguments) {
    char_vec.push_back(const_cast<char*>(arg.c_str()));
  }
  // Grpc keeps the option value as global flag, so we should pass default
  // option value. Default option value could be overwritten by the options
  // given from parameter.
  char_vec.push_back(const_cast<char*>(kDefaultOptionL));
  char_vec.push_back(const_cast<char*>(kDefaultOptionJsonInput));
  char_vec.push_back(const_cast<char*>(kDefaultOptionJsonOutput));
  for (const auto& opt : options) {
    char_vec.push_back(const_cast<char*>(opt.c_str()));
  }
  return char_vec;
}

Result<void> RunGrpcCommand(const std::vector<std::string>& arguments,
                            const std::vector<std::string>& options,
                            std::stringstream& output_stream) {
  auto combined_arguments = CombineArgumentsAndOptions(arguments, options);
  int grpc_cli_argc = combined_arguments.size();
  char** grpc_cli_argv = combined_arguments.data();

  grpc::testing::InitTest(&grpc_cli_argc, &grpc_cli_argv, true);
  CF_EXPECT(
      grpc::testing::GrpcToolMainLib(
          grpc_cli_argc, (const char**)grpc_cli_argv, InsecureCliCredentials(),
          std::bind(PrintStream, &output_stream, std::placeholders::_1)) == 0,
      "gRPC command failed");
  return {};
}

Result<std::string> RunGrpcCommand(const std::vector<std::string>& arguments,
                                   const std::vector<std::string>& options) {
  std::stringstream output_stream;
  CF_EXPECT(RunGrpcCommand(arguments, options, output_stream));
  return output_stream.str();
}

Result<std::string> RunGrpcCommand(const std::vector<std::string>& arguments) {
  std::vector<std::string> options;
  return RunGrpcCommand(arguments, options);
}

Result<std::vector<std::string>> GetServiceList(
    const std::string& server_address) {
  std::vector<std::string> service_list;
  std::stringstream output_stream;

  std::vector<std::string> arguments{"grpc_cli", "ls", server_address};
  std::vector<std::string> options;
  CF_EXPECT(RunGrpcCommand(arguments, options, output_stream));

  std::string service_name;
  while (std::getline(output_stream, service_name)) {
    if (service_name.compare(kServiceServerReflection) == 0 ||
        service_name.compare(kServiceHealth) == 0) {
      continue;
    }
    service_list.emplace_back(service_name);
  }

  return service_list;
}

Result<std::string> GetServerAddress(
    const std::vector<std::string>& server_address_list,
    const std::string& service_name) {
  std::vector<std::string> candidates;
  for (const auto& server_address : server_address_list) {
    auto service_names = CF_EXPECT(GetServiceList(server_address));
    for (auto& full_service_name : service_names) {
      if (EndsWith(full_service_name, service_name)) {
        candidates.emplace_back(server_address);
        break;
      }
    }
  }

  CF_EXPECT(candidates.size() > 0, service_name + " is not found.");
  CF_EXPECT(candidates.size() < 2, service_name + " is ambiguous.");

  return candidates[0];
}

Result<std::string> GetFullServiceName(const std::string& server_address,
                                       const std::string& service_name) {
  std::vector<std::string> candidates;
  auto service_names = CF_EXPECT(GetServiceList(server_address));
  for (auto& full_service_name : service_names) {
    if (EndsWith(full_service_name, service_name)) {
      candidates.emplace_back(full_service_name);
    }
  }

  CF_EXPECT(candidates.size() > 0, service_name + " is not found.");
  CF_EXPECT(candidates.size() < 2, service_name + " is ambiguous.");

  return candidates[0];
}

Result<std::string> GetFullMethodName(const std::string& server_address,
                                      const std::string& service_name,
                                      const std::string& method_name) {
  const auto& full_service_name =
      CF_EXPECT(GetFullServiceName(server_address, service_name));
  return full_service_name + "/" + method_name;
}

Result<std::string> HandleLsCmd(
    const std::vector<std::string>& server_address_list,
    const std::vector<std::string>& args) {
  switch (args.size()) {
    case 0: {
      // ls subcommand with no arguments
      std::string command_output;
      for (const auto& server_address : server_address_list) {
        std::vector<std::string> grpc_arguments{"grpc_cli", "ls",
                                                server_address};
        command_output += CF_EXPECT(RunGrpcCommand(grpc_arguments));
      }

      Json::Value json;
      json["services"] = Json::Value(Json::arrayValue);
      for (auto& full_service_name : Split(Trim(command_output), "\n")) {
        if (full_service_name.compare(kServiceServerReflection) == 0 ||
            full_service_name.compare(kServiceHealth) == 0 ||
            full_service_name.compare(kServiceControlEnvProxyFull) == 0) {
          continue;
        }
        json["services"].append(Split(full_service_name, ".").back());
      }
      Json::StreamWriterBuilder builder;
      return Json::writeString(builder, json) + "\n";
    }
    case 1: {
      // ls subcommand with 1 argument; service_name
      const auto& service_name = args[0];
      CF_EXPECT(service_name.compare(kServiceControlEnvProxy) != 0,
                "Prohibited service name");
      const auto& server_address =
          CF_EXPECT(GetServerAddress(server_address_list, service_name));
      const auto& full_service_name =
          CF_EXPECT(GetFullServiceName(server_address, service_name));
      std::vector<std::string> grpc_arguments{"grpc_cli", "ls", server_address,
                                              full_service_name};
      std::string command_output = CF_EXPECT(RunGrpcCommand(grpc_arguments));

      Json::Value json;
      json["methods"] = Json::Value(Json::arrayValue);
      for (auto& method_name : Split(Trim(command_output), "\n")) {
        json["methods"].append(method_name);
      }
      Json::StreamWriterBuilder builder;
      return Json::writeString(builder, json) + "\n";
    }
    case 2: {
      // ls subcommand with 2 arguments; service_name and method_name
      const auto& service_name = args[0];
      CF_EXPECT(service_name.compare(kServiceControlEnvProxy) != 0,
                "Prohibited service name");
      const auto& server_address =
          CF_EXPECT(GetServerAddress(server_address_list, service_name));
      const auto& method_name = args[1];
      const auto& full_method_name = CF_EXPECT(
          GetFullMethodName(server_address, service_name, method_name));
      std::vector<std::string> grpc_arguments{"grpc_cli", "ls", server_address,
                                              full_method_name};
      std::vector<std::string> options{"-l"};
      std::string command_output =
          CF_EXPECT(RunGrpcCommand(grpc_arguments, options));

      // Example command_output:
      //   rpc SetTxpower(wmediumdserver.SetTxpowerRequest) returns
      // (google.protobuf.Empty) {}
      std::vector<std::string> parsed_output =
          Split(Trim(command_output), "()");
      CF_EXPECT(parsed_output.size() == 5, "Unexpected parsing result");
      Json::Value json;
      json["request_type"] = parsed_output[1];
      json["response_type"] = parsed_output[3];
      Json::StreamWriterBuilder builder;
      return Json::writeString(builder, json) + "\n";
    }
    default:
      return CF_ERR("too many arguments");
  }
}

Result<std::string> HandleTypeCmd(
    const std::vector<std::string>& server_address_list,
    const std::vector<std::string>& args) {
  CF_EXPECT(args.size() > 1,
            "need to specify the service name and the type_name");
  CF_EXPECT(args.size() < 3, "too many arguments");

  std::vector<std::string> grpc_arguments{"grpc_cli", "type"};
  const auto& service_name = args[0];
  CF_EXPECT(service_name.compare(kServiceControlEnvProxy) != 0,
            "Prohibited service name");
  const auto& type_name = args[1];

  const auto& server_address =
      CF_EXPECT(GetServerAddress(server_address_list, service_name));
  grpc_arguments.push_back(server_address);
  grpc_arguments.push_back(type_name);

  return RunGrpcCommand(grpc_arguments);
}

Result<std::string> HandleCallCmd(
    const std::vector<std::string>& server_address_list,
    const std::vector<std::string>& args) {
  CF_EXPECT(args.size() > 2,
            "need to specify the service name, the method name, and the "
            "json-formatted proto");
  CF_EXPECT(args.size() < 4, "too many arguments");

  std::vector<std::string> grpc_arguments{"grpc_cli", "call"};
  // TODO(b/265384449): support calling streaming method.
  const auto& service_name = args[0];
  const auto& method_name = args[1];
  const auto& json_format_proto = args[2];

  const auto& server_address =
      CF_EXPECT(GetServerAddress(server_address_list, service_name));
  grpc_arguments.push_back(server_address);
  const auto& full_method_name =
      CF_EXPECT(GetFullMethodName(server_address, service_name, method_name));
  grpc_arguments.push_back(full_method_name);
  grpc_arguments.push_back(json_format_proto);

  return RunGrpcCommand(grpc_arguments);
}

}  // namespace

Result<std::string> HandleCmds(const std::string& grpc_socket_path,
                               const std::string& cmd,
                               const std::vector<std::string>& args) {
  std::vector<std::string> server_address_list;
  for (const auto& entry :
       std::filesystem::directory_iterator(grpc_socket_path)) {
    LOG(DEBUG) << "loading " << entry.path();
    server_address_list.emplace_back("unix:" + entry.path().string());
  }

  auto command_map =
      std::unordered_map<std::string, std::function<Result<std::string>(
                                          const std::vector<std::string>&,
                                          const std::vector<std::string>&)>>{{
          {"call", HandleCallCmd},
          {"ls", HandleLsCmd},
          {"type", HandleTypeCmd},
      }};
  CF_EXPECT(Contains(command_map, cmd), cmd << " isn't supported");

  auto command_output = CF_EXPECT(command_map[cmd](server_address_list, args));
  return command_output;
}

}  // namespace cuttlefish
