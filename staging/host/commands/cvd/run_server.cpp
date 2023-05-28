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

#include <memory>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/logger.h"
#include "host/commands/cvd/server.h"

namespace cuttlefish {

bool IsServerModeExpected(const std::string& exec_file) {
  return exec_file == kServerExecPath;
}

Result<void> RunServer(const RunServerParam& params) {
  if (!params.internal_server_fd->IsOpen()) {
    return CF_ERR(
        "Expected to be in server mode, but didn't get a server "
        "fd: "
        << params.internal_server_fd->StrError());
  }
  std::unique_ptr<ServerLogger> server_logger =
      std::make_unique<ServerLogger>();
  CF_EXPECT(server_logger != nullptr, "ServerLogger memory allocation failed.");

  const auto& verbosity_level = params.verbosity_level;
  SharedFD stderr_fd =
      (params.carryover_stderr_fd->IsOpen() ? params.carryover_stderr_fd
                                            : SharedFD::Dup(2));
  std::unique_ptr<ServerLogger::ScopedLogger> run_server_logger;
  if (verbosity_level) {
    ServerLogger::ScopedLogger tmp_logger =
        server_logger->LogThreadToFd(std::move(stderr_fd), *verbosity_level);
    run_server_logger =
        std::make_unique<ServerLogger::ScopedLogger>(std::move(tmp_logger));
  } else {
    ServerLogger::ScopedLogger tmp_logger =
        server_logger->LogThreadToFd(std::move(stderr_fd));
    run_server_logger =
        std::make_unique<ServerLogger::ScopedLogger>(std::move(tmp_logger));
  }

  if (params.memory_carryover_fd && !(*params.memory_carryover_fd)->IsOpen()) {
    LOG(ERROR) << "Memory carryover file is supposed to be open but is not.";
  }
  CF_EXPECT(CvdServerMain(
      {.internal_server_fd = params.internal_server_fd,
       .carryover_client_fd = params.carryover_client_fd,
       .memory_carryover_fd = params.memory_carryover_fd,
       .acloud_translator_optout = params.acloud_translator_optout,
       .server_logger = std::move(server_logger),
       .scoped_logger = std::move(run_server_logger)}));
  return {};
}

Result<ParseResult> ParseIfServer(std::vector<std::string>& all_args) {
  std::vector<Flag> flags;
  SharedFD internal_server_fd;
  flags.emplace_back(SharedFDFlag("INTERNAL_server_fd", internal_server_fd));
  SharedFD carryover_client_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_carryover_client_fd", carryover_client_fd));
  SharedFD carryover_stderr_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_carryover_stderr_fd", carryover_stderr_fd));
  SharedFD memory_carryover_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_memory_carryover_fd", memory_carryover_fd));
  // the server's default verbosity must be VERBOSE, the least LogSeverity
  // the LogSeverity control will be done later on by the server by masking
  std::string verbosity = "VERBOSE";
  flags.emplace_back(GflagsCompatFlag("verbosity", verbosity));
  CF_EXPECT(ParseFlags(flags, all_args));

  // now the three flags above are all consumed from all_args
  std::optional<bool> acloud_translator_optout_opt;
  const auto all_args_size_before = all_args.size();
  bool acloud_translator_optout_value = false;
  flags.emplace_back(GflagsCompatFlag("INTERNAL_acloud_translator_optout",
                                      acloud_translator_optout_value));
  CF_EXPECT(ParseFlags({GflagsCompatFlag("INTERNAL_acloud_translator_optout",
                                         acloud_translator_optout_value)},
                       all_args));
  if (all_args.size() != all_args_size_before) {
    acloud_translator_optout_opt = acloud_translator_optout_value;
  }

  std::optional<SharedFD> memory_carryover_fd_opt;
  if (memory_carryover_fd->IsOpen()) {
    memory_carryover_fd_opt = std::move(memory_carryover_fd);
  }

  std::optional<android::base::LogSeverity> verbosity_level;
  if (!verbosity.empty()) {
    verbosity_level = CF_EXPECT(EncodeVerbosity(verbosity));
  }

  ParseResult result = {
      .internal_server_fd = internal_server_fd,
      .carryover_client_fd = carryover_client_fd,
      .memory_carryover_fd = memory_carryover_fd_opt,
      .carryover_stderr_fd = carryover_stderr_fd,
      .acloud_translator_optout = acloud_translator_optout_opt,
      .verbosity_level = verbosity_level,
  };
  return {result};
}

}  // namespace cuttlefish
