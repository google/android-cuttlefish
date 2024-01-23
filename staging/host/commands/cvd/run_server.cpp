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

#include "host/commands/cvd/run_server.h"

#include <unistd.h>

#include <memory>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/logger.h"
#include "host/commands/cvd/metrics/metrics_notice.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_constants.h"

namespace cuttlefish {

SharedFD ServerMainLog() {
  std::string log_path = "/tmp/cvd_server" + std::to_string(getuid()) + ".log";
  return SharedFD::Open(log_path, O_CREAT | O_APPEND | O_RDWR, 0644);
}

bool IsServerModeExpected(const std::string& exec_file) {
  return exec_file == kServerExecPath;
}

Result<void> RunServer(RunServerParam&& params) {
  CF_EXPECTF(params.internal_server_fd->IsOpen(),
             "Expected to be in server mode, but didn't get a server fd: {}",
             params.internal_server_fd->StrError());

  std::unique_ptr<ServerLogger> server_logger =
      std::make_unique<ServerLogger>();
  CF_EXPECT(server_logger != nullptr, "ServerLogger memory allocation failed.");

  const auto verbosity_level = params.verbosity_level;
  // TODO(kwstephenkim): for cvd restart-server, it should print the LOG(ERROR)
  // of the server codes outside handlers into the file descriptor eventually
  // passed from the cvd restart client. However, the testing frameworks are
  // waiting for the client's stderr to be closed. Thus, it should not be the
  // client's stderr. See b/293191537.
  SharedFD stderr_fd = ServerMainLog();
  std::unique_ptr<ServerLogger::ScopedLogger> run_server_logger;
  if (stderr_fd->IsOpen()) {
    ServerLogger::ScopedLogger tmp_logger =
        (verbosity_level ? server_logger->LogThreadToFd(std::move(stderr_fd),
                                                        *verbosity_level)
                         : server_logger->LogThreadToFd(std::move(stderr_fd)));
    run_server_logger =
        std::make_unique<ServerLogger::ScopedLogger>(std::move(tmp_logger));
  }

  // run_server_logger will be destroyed only if CvdServerMain terminates, which
  // normally does not. And, CvdServerMain does not refer its .scoped_logger.
  if (params.memory_carryover_fd && !(*params.memory_carryover_fd)->IsOpen()) {
    LOG(ERROR) << "Memory carryover file is supposed to be open but is not.";
  }
  CF_EXPECT(CvdServerMain(
      {.internal_server_fd = std::move(params.internal_server_fd),
       .carryover_client_fd = std::move(params.carryover_client_fd),
       .memory_carryover_fd = std::move(params.memory_carryover_fd),
       .acloud_translator_optout = params.acloud_translator_optout,
       .server_logger = std::move(server_logger),
       .scoped_logger = std::move(run_server_logger),
       .restarted_in_process = params.restarted_in_process}));
  return {};
}

Result<ParseResult> ParseIfServer(std::vector<std::string>& all_args) {
  ParseResult result;
  std::vector<Flag> flags;
  flags.emplace_back(
      SharedFDFlag(kInternalServerFd, result.internal_server_fd));
  flags.emplace_back(
      SharedFDFlag(kInternalCarryoverClientFd, result.carryover_client_fd));
  SharedFD memory_carryover_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_memory_carryover_fd", memory_carryover_fd));
  // the server's default verbosity must be VERBOSE, the least LogSeverity
  // the LogSeverity control will be done later on by the server by masking
  std::string verbosity = "VERBOSE";
  flags.emplace_back(GflagsCompatFlag("verbosity", verbosity));
  result.restarted_in_process = false;
  flags.emplace_back(GflagsCompatFlag(kInternalRestartedInProcess,
                                      result.restarted_in_process));
  CF_EXPECT(ParseFlags(flags, all_args));

  // now the flags above consumed their lexical tokens from all_args
  // For now, the default value of acloud_translator_optout is false
  // In the future, it might be determined by the server if not given.
  const auto all_args_size_before = all_args.size();
  bool acloud_translator_optout_value = false;
  PrintDataCollectionNotice();
  flags.emplace_back(GflagsCompatFlag("INTERNAL_acloud_translator_optout",
                                      acloud_translator_optout_value));
  CF_EXPECT(ParseFlags({GflagsCompatFlag("INTERNAL_acloud_translator_optout",
                                         acloud_translator_optout_value)},
                       all_args));
  if (all_args.size() != all_args_size_before) {
    result.acloud_translator_optout = acloud_translator_optout_value;
  }

  if (memory_carryover_fd->IsOpen()) {
    result.memory_carryover_fd = std::move(memory_carryover_fd);
  }

  if (!verbosity.empty()) {
    result.verbosity_level = CF_EXPECT(EncodeVerbosity(verbosity));
  }

  return result;
}

}  // namespace cuttlefish
