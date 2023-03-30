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
#include <iterator>
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
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/frontline_parser.h"
#include "host/commands/cvd/handle_reset.h"
#include "host/commands/cvd/reset_client_utils.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_constants.h"
#include "host/commands/cvd/types.h"
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

bool IsServerModeExpected(const std::string& exec_file) {
  return exec_file == kServerExecPath;
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
  SharedFD internal_server_fd_;
  SharedFD carryover_client_fd_;
};

Result<ParseResult> ParseIfServer(std::vector<std::string>& all_args) {
  std::vector<Flag> flags;
  SharedFD internal_server_fd;
  flags.emplace_back(SharedFDFlag("INTERNAL_server_fd", internal_server_fd));
  SharedFD carryover_client_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_carryover_client_fd", carryover_client_fd));

  CF_EXPECT(ParseFlags(flags, all_args));
  ParseResult result = {internal_server_fd, carryover_client_fd};
  return {result};
}

Result<FlagCollection> CvdFlags() {
  FlagCollection cvd_flags;
  cvd_flags.EnrollFlag(CvdFlag<bool>("clean", false));
  cvd_flags.EnrollFlag(CvdFlag<bool>("help", false));
  return cvd_flags;
}

Result<bool> FilterDriverHelpOptions(const FlagCollection& cvd_flags,
                                     cvd_common::Args& cvd_args) {
  auto help_flag = CF_EXPECT(cvd_flags.GetFlag("help"));
  bool is_help = CF_EXPECT(help_flag.CalculateFlag<bool>(cvd_args));
  return is_help;
}

cvd_common::Args AllArgs(const std::string& prog_path,
                         const cvd_common::Args& cvd_args,
                         const std::optional<std::string>& subcmd,
                         const cvd_common::Args& subcmd_args) {
  std::vector<std::string> all_args;
  all_args.push_back(prog_path);
  all_args.insert(all_args.end(), cvd_args.begin(), cvd_args.end());
  if (subcmd) {
    all_args.push_back(*subcmd);
  }
  all_args.insert(all_args.end(), subcmd_args.begin(), subcmd_args.end());
  return all_args;
}

struct ClientCommandCheckResult {
  bool was_client_command_;
  cvd_common::Args new_all_args;
};
Result<ClientCommandCheckResult> HandleClientCommands(
    CvdClient& client, const cvd_common::Args& all_args) {
  ClientCommandCheckResult output;
  std::vector<std::string> client_internal_commands{"kill-server",
                                                    "server-kill", "reset"};
  FlagCollection cvd_flags = CF_EXPECT(CvdFlags());
  FrontlineParser::ParserParam client_param{
      .server_supported_subcmds = std::vector<std::string>{},
      .internal_cmds = client_internal_commands,
      .all_args = all_args,
      .cvd_flags = cvd_flags};
  auto client_parser_result = FrontlineParser::Parse(client_param);
  if (!client_parser_result.ok()) {
    return ClientCommandCheckResult{.was_client_command_ = false,
                                    .new_all_args = all_args};
  }

  auto client_parser = std::move(*client_parser_result);
  CF_EXPECT(client_parser != nullptr);
  auto cvd_args = client_parser->CvdArgs();
  auto is_help = CF_EXPECT(FilterDriverHelpOptions(cvd_flags, cvd_args));
  output.new_all_args =
      AllArgs(client_parser->ProgPath(), cvd_args, client_parser->SubCmd(),
              client_parser->SubCmdArgs());
  output.was_client_command_ = (!is_help && client_parser->SubCmd());
  if (!output.was_client_command_) {
    // could be simply "cvd"
    output.new_all_args = cvd_common::Args{"cvd", "help"};
    return output;
  }

  // Special case for `cvd kill-server`, handled by directly
  // stopping the cvd_server.
  std::vector<std::string> kill_server_cmds{"kill-server", "server-kill"};
  std::string subcmd = client_parser->SubCmd().value_or("");
  if (Contains(kill_server_cmds, subcmd)) {
    CF_EXPECT(client.StopCvdServer(/*clear=*/true));
    return output;
  }
  CF_EXPECT_EQ(subcmd, "reset", "unsupported subcmd: " << subcmd);
  CF_EXPECT(HandleReset(client, client_parser->SubCmdArgs()));
  return output;
}

Result<void> CvdMain(int argc, char** argv, char** envp) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  cvd_common::Args all_args = ArgsToVec(argc, argv);
  CF_EXPECT(!all_args.empty());

  auto env = EnvVectorToMap(envp);
  const auto host_tool_dir =
      android::base::Dirname(android::base::GetExecutableDirectory());

  if (android::base::Basename(all_args[0]) == "fetch_cvd") {
    CF_EXPECT(FetchCvdMain(argc, argv));
    return {};
  }

  CvdClient client;
  // TODO(b/206893146): Make this decision inside the server.
  if (android::base::Basename(all_args[0]) == "acloud") {
    return client.HandleAcloud(all_args, env, host_tool_dir);
  }

  if (IsServerModeExpected(all_args[0])) {
    auto [internal_server_fd, carryover_client_fd] =
        CF_EXPECT(ParseIfServer(all_args));
    return RunServer(internal_server_fd, carryover_client_fd);
  }

  CF_EXPECT_EQ(android::base::Basename(all_args[0]), "cvd");

  // TODO(kwstephenkim): --help should be handled here.
  // And, the FrontlineParser takes any positional argument as
  // a valid subcommand.

  auto [was_client_command, new_all_args] =
      CF_EXPECT(HandleClientCommands(client, all_args));
  if (was_client_command) {
    return {};
  }
  /*
   * For now, the parser needs a running server. The parser will
   * be moved to the server side, and then it won't.
   *
   */
  CF_EXPECT(client.ValidateServerVersion(host_tool_dir),
            "Unable to ensure cvd_server is running.");

  std::vector<std::string> version_command{"version"};
  FlagCollection cvd_flags = CF_EXPECT(CvdFlags());
  FrontlineParser::ParserParam version_param{
      .server_supported_subcmds = std::vector<std::string>{},
      .internal_cmds = version_command,
      .all_args = new_all_args,
      .cvd_flags = cvd_flags};
  auto version_parser_result = FrontlineParser::Parse(version_param);
  if (version_parser_result.ok()) {
    auto version_parser = std::move(*version_parser_result);
    CF_EXPECT(version_parser != nullptr);
    const auto subcmd = version_parser->SubCmd().value_or("");
    if (subcmd == "version") {
      auto version_msg = CF_EXPECT(client.HandleVersion(host_tool_dir));
      std::cout << version_msg;
      return {};
    }
    CF_EXPECT(subcmd.empty(),
              "subcmd is expected to be \"\" but is " << subcmd);
  }

  const cvd_common::Args new_cmd_args{"cvd", "process"};
  CF_EXPECT(!new_all_args.empty());
  const cvd_common::Args new_selector_args{new_all_args.begin(),
                                           new_all_args.end()};
  // TODO(schuffelen): Deduplicate when calls to setenv are removed.
  CF_EXPECT(client.HandleCommand(new_cmd_args, env, new_selector_args));
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv, char** envp) {
  auto result = cuttlefish::CvdMain(argc, argv, envp);
  if (result.ok()) {
    return 0;
  } else {
    std::cerr << result.error().Trace() << std::endl;
    return -1;
  }
}
