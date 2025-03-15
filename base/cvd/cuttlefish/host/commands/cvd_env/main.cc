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
    "  e.g. Wmediumd & OpenWRT for Wifi, GNSS for geolocation\n"
    "\n"
    "Please visit following link for the more information.\n"
    "https://source.android.com/docs/setup/create/"
    "cuttlefish-control-environment\n"
    "\n"
    "Basic usage: cvd [SELECTOR_OPTIONS] env [SUBCOMMAND] [ARGS]\n"
    "\n"
    "Subcommands:\n"
    "  ls: List available services or methods\n"
    "    Get a list of all services:\n"
    "      Usage: cvd [SELECTOR_OPTIONS] env ls\n"
    "    Get a list of all methods for a service:\n"
    "      Usage: cvd [SELECTOR_OPTIONS] env ls [SERVICE_NAME]\n"
    "    Get detailed information like request or response message types of a "
    "method:\n"
    "      Usage: cvd [SELECTOR_OPTIONS] env ls [SERVICE_NAME] [METHOD_NAME]\n"
    "  call: Send RPC request to make changes to the environment\n"
    "      Usage: cvd [SELECTOR_OPTIONS] env call [SERVICE_NAME] [METHOD_NAME] "
    "[JSON_FORMATTED_PROTO]\n"
    "  type: Get detailed information on message types\n"
    "      Usage: cvd [SELECTOR_OPTIONS] env type [SERVICE_NAME] [TYPE_NAME]\n"
    "\n"
    "Arguments:\n"
    "  SERVICE_NAME         : gRPC service name\n"
    "  METHOD_NAME          : method name in given service\n"
    "  TYPE_NAME            : Protobuf message type name including request or "
    "response messages\n"
    "  JSON_FORMATTED_PROTO : Protobuf message data with JSON format\n"
    "\n"
    "* \"cvd [SELECTOR_OPTIONS] env\" can be replaced with: \"cvd_internal_env "
    "[INTERNAL_DEVICE_NAME]\"\n";

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
