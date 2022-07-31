/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <stdlib.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/result.h>
#include <google/protobuf/text_format.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/client.h"
#include "host/commands/cvd/fetch_cvd.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/host_tools_version.h"

namespace cuttlefish {
namespace {

[[noreturn]] void CallPythonAcloud(std::vector<std::string>& args) {
  auto android_top = StringFromEnv("ANDROID_BUILD_TOP", "");
  if (android_top == "") {
    LOG(FATAL) << "Could not find android environment. Please run "
               << "\"source build/envsetup.sh\".";
    abort();
  }
  // TODO(b/206893146): Detect what the platform actually is.
  auto py_acloud_path =
      android_top + "/prebuilts/asuite/acloud/linux-x86/acloud";
  char** new_argv = new char*[args.size() + 1];
  for (size_t i = 0; i < args.size(); i++) {
    new_argv[i] = args[i].data();
  }
  new_argv[args.size()] = nullptr;
  execv(py_acloud_path.data(), new_argv);
  PLOG(FATAL) << "execv(" << py_acloud_path << ", ...) failed";
  abort();
}

Result<void> CvdMain(int argc, char** argv, char** envp) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  std::vector<std::string> args = ArgsToVec(argc, argv);
  std::vector<Flag> flags;

  CvdClient client;

  if (android::base::Basename(args[0]) == "fetch_cvd") {
    CF_EXPECT(FetchCvdMain(argc, argv));
    return {};
  }

  // TODO(b/206893146): Make this decision inside the server.
  if (android::base::Basename(args[0]) == "acloud") {
    auto server_running = client.ValidateServerVersion(
        android::base::Dirname(android::base::GetExecutableDirectory()));
    if (server_running.ok()) {
      // TODO(schuffelen): Deduplicate when calls to setenv are removed.
      std::vector<std::string> env;
      for (char** e = envp; *e != 0; e++) {
        env.emplace_back(*e);
      }
      args[0] = "try-acloud";
      auto attempt = client.HandleCommand(args, env);
      if (attempt.ok()) {
        args[0] = "acloud";
        CF_EXPECT(client.HandleCommand(args, env));
        return {};
      } else {
        CallPythonAcloud(args);
      }
    } else {
      // Something is wrong with the server, fall back to python acloud
      CallPythonAcloud(args);
    }
  }

  bool clean = false;
  flags.emplace_back(GflagsCompatFlag("clean", clean));
  SharedFD internal_server_fd;
  flags.emplace_back(SharedFDFlag("INTERNAL_server_fd", internal_server_fd));
  SharedFD carryover_client_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_carryover_client_fd", carryover_client_fd));

  CF_EXPECT(ParseFlags(flags, args));

  if (internal_server_fd->IsOpen()) {
    CF_EXPECT(CvdServerMain(internal_server_fd, carryover_client_fd));
    return {};
  } else if (argv[0] == std::string("/proc/self/exe")) {
    return CF_ERR(
        "Expected to be in server mode, but didn't get a server "
        "fd: "
        << internal_server_fd->StrError());
  }

  // Special case for `cvd kill-server`, handled by directly
  // stopping the cvd_server.
  if (argc > 1 && strcmp("kill-server", argv[1]) == 0) {
    CF_EXPECT(client.StopCvdServer(/*clear=*/true));
    return {};
  }

  // Special case for --clean flag, used to clear any existing state.
  if (clean) {
    LOG(INFO) << "cvd invoked with --clean; "
              << "stopping the cvd_server before continuing.";
    CF_EXPECT(client.StopCvdServer(/*clear=*/true));
  }

  auto dir = android::base::Dirname(android::base::GetExecutableDirectory());

  // Handle all remaining commands by forwarding them to the cvd_server.
  CF_EXPECT(client.ValidateServerVersion(dir),
            "Unable to ensure cvd_server is running.");

  // Special case for `cvd version`, handled by using the version command.
  if (argc > 1 && std::string(argv[0]) == "cvd" &&
      std::string(argv[1]) == "version") {
    using google::protobuf::TextFormat;

    std::string output;
    auto server_version = CF_EXPECT(client.GetServerVersion(dir));
    TextFormat::PrintToString(server_version, &output);
    std::cout << "Server version:\n\n" << output << "\n";

    TextFormat::PrintToString(client.GetClientVersion(), &output);
    std::cout << "Client version:\n\n" << output << "\n";
    return {};
  }

  // TODO(schuffelen): Deduplicate when calls to setenv are removed.
  std::vector<std::string> env;
  for (char** e = envp; *e != 0; e++) {
    env.emplace_back(*e);
  }
  CF_EXPECT(client.HandleCommand(args, env));
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv, char** envp) {
  auto result = cuttlefish::CvdMain(argc, argv, envp);
  if (result.ok()) {
    return 0;
  } else {
    std::cerr << result.error() << std::endl;
    return -1;
  }
}
