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

#include <iostream>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/control_env/grpc_service_handler.h"

namespace cuttlefish {
namespace {

constexpr char kCvdEnvHelpMessage[] =
    "cvd env: cuttlefish environment controller\n"
    "Basic usage: cvd [selector options] env [sub_command] [args] [options]\n"
    "Sub commands:\n"
    "  ls: list services and methods for given arguments\n"
    "    Usage: cvd [selector options] env ls [service] [method] [-l]\n"
    "      service(optional) : gRPC service name\n"
    "      method(optional)  : method name for given service\n"
    "  type: get detailed information for given request/reply type\n"
    "    Usage: cvd [selector options] env type [service] [method] [type]\n"
    "      service           : gRPC service name\n"
    "      method            : method name in given service\n"
    "      type              : Protocol buffer type name in given method\n"
    "  call: request a rpc with given method\n"
    "    Usage: cvd [selector options] env call [service] [method] [request]\n"
    "      service           : gRPC service name\n"
    "      method            : method name in given service\n"
    "      request           : Protobuffer with json format\n\n"
    "* \"cvd [selector_options] env\" can be replaced with:\n"
    "    \"cvd_internal_env [internal device name]\"\n";
constexpr char kServiceControlEnvProxy[] = "ControlEnvProxyService";

bool ContainHelpOption(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-help") == 0) {
      return true;
    }
  }
  return false;
}

Result<void> CvdEnvMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  if (ContainHelpOption(argc, argv)) {
    std::cout << kCvdEnvHelpMessage;
    return {};
  }

  CF_EXPECT(argc >= 3, " need to specify a receiver and a command");
  const auto& receiver = argv[1];
  const auto& cmd = argv[2];

  std::vector<std::string> args;
  for (int i = 3; i < argc; i++) {
    // Ignore options, not to be applied when calling grpc_cli.
    if (!android::base::StartsWith(argv[i], '-')) {
      args.push_back(argv[i]);
    }
  }
  if (args.size() > 0) {
    CF_EXPECT(args[0].compare(kServiceControlEnvProxy) != 0,
              "Prohibited service name");
  }

  const auto* config = CuttlefishConfig::Get();
  CF_EXPECT(config != nullptr, "Unable to find the config");
  const auto& instances = config->Instances();
  auto receiver_instance = std::find_if(
      begin(instances), end(instances), [&receiver](const auto& instance) {
        return receiver == instance.instance_name();
      });

  CF_EXPECT(receiver_instance != std::end(instances),
            "there is no instance of which name is "
                << receiver << ". please check instance name by cvd fleet");

  auto command_output =
      CF_EXPECT(HandleCmds(receiver_instance->grpc_socket_path(), cmd, args));

  std::cout << command_output;

  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  const auto& ret = cuttlefish::CvdEnvMain(argc, argv);
  CHECK(ret.ok()) << ret.error().FormatForEnv();
  return 0;
}
