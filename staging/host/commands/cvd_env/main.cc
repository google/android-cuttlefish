/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <android-base/logging.h>

#include <gflags/gflags.h>
#include <unordered_map>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <test/cpp/util/grpc_tool.h>
#include <filesystem>
#include "host/libs/config/cuttlefish_config.h"

using grpc::InsecureChannelCredentials;

DECLARE_string(call_creds);

namespace grpc {
namespace testing {
DECLARE_bool(l);
}  // namespace testing
}  // namespace grpc

namespace cuttlefish {
namespace {

bool PrintStream(std::stringstream* ss, const grpc::string& output) {
  (*ss) << output;
  return true;
}

class InsecureCliCredentials final : public grpc::testing::CliCredentials {
 public:
  std::shared_ptr<grpc::ChannelCredentials> GetChannelCredentials()
      const override {
    return InsecureChannelCredentials();
  }
  const grpc::string GetCredentialUsage() const override { return ""; }
};

std::vector<std::string> GetServiceList(const std::string& server_address) {
  std::vector<std::string> service_list;
  std::stringstream output_stream;

  grpc::testing::FLAGS_l = false;
  const char* new_argv[] = {"grpc_cli", "ls", server_address.c_str()};
  grpc::testing::GrpcToolMainLib(
      std::size(new_argv), new_argv, InsecureCliCredentials(),
      std::bind(PrintStream, &output_stream, std::placeholders::_1));

  std::string service_name;
  while (std::getline(output_stream, service_name)) {
    if (service_name.compare("grpc.reflection.v1alpha.ServerReflection") == 0) {
      continue;
    }
    service_list.emplace_back(service_name);
  }
  return service_list;
}

std::string CallMethod(const std::string& server_address,
                       const std::string& service_name,
                       const std::string& method_name,
                       const std::string& proto_text_format) {
  std::stringstream output_stream;
  const std::string formatted_method_name = service_name + "/" + method_name;
  const char* new_argv[] = {"grpc_cli", "call", server_address.c_str(),
                            formatted_method_name.c_str(),
                            proto_text_format.c_str()};

  GrpcToolMainLib(
      std::size(new_argv), new_argv, InsecureCliCredentials(),
      std::bind(PrintStream, &output_stream, std::placeholders::_1));

  return output_stream.str();
}

bool HandleLsCmd(const std::vector<std::string>& server_address_list,
                 const std::vector<std::string>& args) {
  // TODO(b/264201498)
  LOG(INFO) << "TODO(b/264201498)";
  return true;
}

bool HandleTypeCmd(const std::vector<std::string>& server_address_list,
                   const std::vector<std::string>& args) {
  // TODO(b/264201498)
  LOG(INFO) << "TODO(b/264201498)";
  return true;
}

bool HandleCallCmd(const std::vector<std::string>& server_address_list,
                   const std::vector<std::string>& args) {
  if (args.size() < 3) {
    LOG(ERROR) << "need to specify a service name, a method name, and "
                  "text-formatted proto";
    return false;
  }
  const auto& service_name = args[0];
  const auto& method_name = args[1];
  const auto& proto_text_format = args[2];
  std::string candidate;
  bool found = false;
  std::unordered_map<std::string, std::vector<std::string>> address_service_map;

  for (const auto& server_address : server_address_list) {
    address_service_map[server_address] = GetServiceList(server_address);
  }
  for (const auto& server_address : server_address_list) {
    const auto& service_list = address_service_map[server_address];
    // exact match
    if (std::find(service_list.begin(), service_list.end(), service_name) ==
        service_list.end()) {
      continue;
    }
    candidate = server_address;
    found = true;
  }
  if (!found) {
    for (const auto& server_address : server_address_list) {
      const auto& service_list = address_service_map[server_address];
      for (auto& s : service_list) {
        // partial match
        if (s.find(service_name) != std::string::npos) {
          candidate = server_address;
          found = true;
          break;
        }
      }

      if (found) {
        break;
      }
    }
  }
  if (found) {
    LOG(INFO) << CallMethod(candidate, service_name, method_name,
                            proto_text_format);
  } else {
    LOG(ERROR) << service_name << "." << method_name << "(" << proto_text_format
               << ") is not found";
  }
  return found;
}

int CvdEnvMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  FLAGS_call_creds = "none";

  if (argc < 3) {
    LOG(ERROR) << "need to specify a receiver and a command";
    return -1;
  }
  const auto& receiver = argv[1];
  const auto& cmd = argv[2];
  std::vector<std::string> args;
  for (int i = 3; i < argc; i++) {
    args.push_back(argv[i]);
  }

  const auto& config = CuttlefishConfig::Get();
  CHECK(config) << "Unable to find the config";
  std::vector<std::string> server_address_list;
  const auto& instances = config->Instances();
  auto receiver_instance = std::find_if(
      begin(instances), end(instances), [&receiver](const auto& instance) {
        return receiver == instance.instance_name();
      });

  if (receiver_instance == std::end(instances)) {
    LOG(ERROR) << "there is no instance of which name is " << receiver
               << ". please check instance name by cvd fleet";
    return -1;
  }

  for (const auto& entry : std::filesystem::directory_iterator(
           receiver_instance->grpc_socket_path())) {
    LOG(INFO) << "loading " << entry.path();
    server_address_list.emplace_back("unix:" + entry.path().string());
  }

  auto commandMap =
      std::unordered_map<std::string,
                         std::function<bool(const std::vector<std::string>&,
                                            const std::vector<std::string>&)>>{{
          {"call", HandleCallCmd},
          {"ls", HandleLsCmd},
          {"type", HandleTypeCmd},
      }};

  if (commandMap.count(cmd) == 0) {
    LOG(ERROR) << cmd << " isn't supported";
    return -1;
  }
  if (!commandMap[cmd](server_address_list, args)) {
    LOG(ERROR) << cmd << " fails";
    return -1;
  }
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::CvdEnvMain(argc, argv); }
