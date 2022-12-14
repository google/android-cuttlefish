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

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/result.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "host/commands/cvd/client.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/selector/selector_cmdline_parser.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/host_tools_version.h"

namespace cuttlefish {
namespace {

std::unordered_map<std::string, std::string> EnvVectorToMap(char** envp) {
  std::unordered_map<std::string, std::string> env_map;
  if (!envp) {
    return env_map;
  }
  for (char** e = envp; *e != nullptr; e++) {
    std::string env_var_val(*e);
    auto tokens = android::base::Split(env_var_val, "=");
    if (tokens.size() <= 1) {
      LOG(WARNING) << "Environment var in unknown format: " << env_var_val;
      continue;
    }
    const auto var = tokens.at(0);
    tokens.erase(tokens.begin());
    env_map[var] = android::base::Join(tokens, "=");
  }
  return env_map;
}

bool IsServerModeExpected(const SharedFD& internal_server_fd,
                          const std::string& exec_file) {
  return internal_server_fd->IsOpen() || exec_file == "/proc/self/exe";
}

Result<void> RunServer(const SharedFD& internal_server_fd,
                       const SharedFD& carryover_client_fd) {
  if (!internal_server_fd->IsOpen()) {
    return CF_ERR(
        "Expected to be in server mode, but didn't get a server "
        "fd: "
        << internal_server_fd->StrError());
  }
  CF_EXPECT(CvdServerMain(internal_server_fd, carryover_client_fd));
  return {};
}

struct ParseResult {
  bool clean_;
  SharedFD internal_server_fd_;
  SharedFD carryover_client_fd_;
};

Result<ParseResult> Parse(std::vector<std::string>& args) {
  std::vector<Flag> flags;
  bool clean = false;
  flags.emplace_back(GflagsCompatFlag("clean", clean));
  SharedFD internal_server_fd;
  flags.emplace_back(SharedFDFlag("INTERNAL_server_fd", internal_server_fd));
  SharedFD carryover_client_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_carryover_client_fd", carryover_client_fd));

  CF_EXPECT(ParseFlags(flags, args));
  ParseResult result = {clean, internal_server_fd, carryover_client_fd};
  return {result};
}

Result<void> CvdMain(int argc, char** argv, char** envp) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  std::vector<std::string> all_args = ArgsToVec(argc, argv);

  if (android::base::Basename(all_args[0]) == "fetch_cvd") {
    CF_EXPECT(FetchCvdMain(argc, argv));
    return {};
  }

  auto [args, selector_args] =
      CF_EXPECT(selector::GetCommandAndSelectorArguments(all_args));
  auto env = EnvVectorToMap(envp);

  CvdClient client;

  auto host_tool_dir =
      android::base::Dirname(android::base::GetExecutableDirectory());

  // TODO(b/206893146): Make this decision inside the server.
  if (android::base::Basename(args[0]) == "acloud") {
    return client.HandleAcloud(args, env, host_tool_dir);
  }

  auto [clean, internal_server_fd, carryover_client_fd] =
      CF_EXPECT(Parse(args));

  if (IsServerModeExpected(internal_server_fd, args[0])) {
    return RunServer(internal_server_fd, carryover_client_fd);
  }

  // Special case for `cvd kill-server`, handled by directly
  // stopping the cvd_server.
  std::vector<std::string> kill_server_cmds{"kill-server", "server-kill"};
  if (args.size() > 1 && Contains(kill_server_cmds, args[1])) {
    CF_EXPECT(client.StopCvdServer(/*clear=*/true));
    return {};
  }

  // Special case for --clean flag, used to clear any existing state.
  if (clean) {
    LOG(INFO) << "cvd invoked with --clean; "
              << "stopping the cvd_server before continuing.";
    CF_EXPECT(client.StopCvdServer(/*clear=*/true));
  }

  // Handle all remaining commands by forwarding them to the cvd_server.
  CF_EXPECT(client.ValidateServerVersion(host_tool_dir),
            "Unable to ensure cvd_server is running.");

  // Special case for `cvd version`, handled by using the version command.
  if (args.size() > 1 && android::base::Basename(args[0]) == "cvd" &&
      args[1] == "version") {
    auto version_msg = CF_EXPECT(client.HandleVersion(host_tool_dir));
    std::cout << version_msg;
    return {};
  }

  // TODO(schuffelen): Deduplicate when calls to setenv are removed.
  CF_EXPECT(client.HandleCommand(args, env, selector_args));
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv, char** envp) {
  auto result = cuttlefish::CvdMain(argc, argv, envp);
  if (result.ok()) {
    return 0;
  } else {
    std::cerr << result.error().Message() << std::endl;
    return -1;
  }
}
