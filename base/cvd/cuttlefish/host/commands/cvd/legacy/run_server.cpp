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

#include "host/commands/cvd/legacy/run_server.h"

#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/utils/common.h"
#include "host/commands/cvd/metrics/metrics_notice.h"
#include "host/commands/cvd/instances/instance_database.h"

namespace cuttlefish {

namespace {

inline constexpr char kInternalCarryoverClientFd[] =
    "INTERNAL_carryover_client_fd";
inline constexpr char kInternalMemoryCarryoverFd[] =
    "INTERNAL_memory_carryover_fd";
inline constexpr char kInternalAcloudTranslatorOptOut[] =
    "INTERNAL_acloud_translator_optout";
inline constexpr char kInternalRestartedInProcess[] =
    "INTERNAL_restarted_in_process";

struct ParseResult {
  SharedFD internal_server_fd;
  SharedFD carryover_client_fd;
  std::optional<SharedFD> memory_carryover_fd;
  std::optional<bool> acloud_translator_optout;
  std::optional<android::base::LogSeverity> verbosity_level;
  bool restarted_in_process;
};

Result<ParseResult> ParseIfServer(std::vector<std::string>& all_args) {
  ParseResult result;
  std::vector<Flag> flags;
  flags.emplace_back(
      SharedFDFlag(kInternalServerFd, result.internal_server_fd));
  flags.emplace_back(
      SharedFDFlag(kInternalCarryoverClientFd, result.carryover_client_fd));
  SharedFD memory_carryover_fd;
  flags.emplace_back(
      SharedFDFlag(kInternalMemoryCarryoverFd, memory_carryover_fd));
  // the server's default verbosity must be VERBOSE, the least LogSeverity
  // the LogSeverity control will be done later on by the server by masking
  std::string verbosity = "VERBOSE";
  flags.emplace_back(GflagsCompatFlag("verbosity", verbosity));
  result.restarted_in_process = false;
  flags.emplace_back(GflagsCompatFlag(kInternalRestartedInProcess,
                                      result.restarted_in_process));
  CF_EXPECT(ConsumeFlags(flags, all_args));

  // now the flags above consumed their lexical tokens from all_args
  // For now, the default value of acloud_translator_optout is false
  // In the future, it might be determined by the server if not given.
  const auto all_args_size_before = all_args.size();
  bool acloud_translator_optout_value = true;
  PrintDataCollectionNotice();
  flags.emplace_back(GflagsCompatFlag(kInternalAcloudTranslatorOptOut,
                                      acloud_translator_optout_value));
  CF_EXPECT(ConsumeFlags({GflagsCompatFlag(kInternalAcloudTranslatorOptOut,
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

Result<std::string> ReadAllFromMemFd(const SharedFD& mem_fd) {
  const auto n_message_size = mem_fd->LSeek(0, SEEK_END);
  CF_EXPECT_NE(n_message_size, -1, "LSeek on the memory file failed.");
  std::vector<char> buffer(n_message_size);
  CF_EXPECT_EQ(mem_fd->LSeek(0, SEEK_SET), 0, mem_fd->StrError());
  auto n_read = ReadExact(mem_fd, buffer.data(), n_message_size);
  CF_EXPECT(n_read == n_message_size,
            "Expected to read " << n_message_size << " bytes but actually read "
                                << n_read << " bytes.");
  std::string message(buffer.begin(), buffer.end());
  return message;
}

Result<void> ImportResourcesImpl(const ParseResult& param) {
  SetMinimumVerbosity(android::base::VERBOSE);
  LOG(INFO) << "Starting server";
  signal(SIGPIPE, SIG_IGN);
  selector::InstanceDatabase instance_database(InstanceDatabasePath());
  cvd::Response response;
  if (param.memory_carryover_fd) {
    SharedFD memory_carryover_fd = std::move(*param.memory_carryover_fd);
    auto json_string = CF_EXPECT(ReadAllFromMemFd(memory_carryover_fd),
                                 "Failed to parse JSON from mem fd");
    auto json = CF_EXPECT(ParseJson(json_string));
    CF_EXPECTF(instance_database.LoadFromJson(json), "Failed to load from: {}",
               json_string);
  }
  if (param.acloud_translator_optout) {
    LOG(VERBOSE) << "Acloud translation optout: "
                 << param.acloud_translator_optout.value();
    CF_EXPECT(instance_database.SetAcloudTranslatorOptout(
        param.acloud_translator_optout.value()));
  }
  return {};
}

static Result<UnixMessageSocket> GetClient(const SharedFD& client) {
  UnixMessageSocket result(client);
  CF_EXPECT(result.EnableCredentials(true),
            "Unable to enable UnixMessageSocket credentials.");
  return result;
}

Result<void> SendResponse(const SharedFD& client,
                          const cvd::Response& response) {
  std::string serialized;
  CF_EXPECT(response.SerializeToString(&serialized),
            "Unable to serialize response proto.");
  UnixSocketMessage message;
  message.data = std::vector<char>(serialized.begin(), serialized.end());

  UnixMessageSocket writer =
      CF_EXPECT(GetClient(client), "Couldn't get client");
  CF_EXPECT(writer.WriteMessage(message));
  return {};
}

}  // namespace

bool IsServerModeExpected(const std::string& exec_file) {
  return exec_file == kServerExecPath;
}

[[noreturn]] void ImportResourcesFromRunningServer(
    std::vector<std::string> args) {
  auto parsed_res = ParseIfServer(args);
  if (!parsed_res.ok()) {
    LOG(ERROR) << "Failed to parse arguments: "
               << parsed_res.error().FormatForEnv();
    std::exit(1);
  }
  auto parsed = *parsed_res;
  auto import_res = ImportResourcesImpl(parsed);
  cvd::Response response;
  if (import_res.ok()) {
    response.mutable_status()->set_code(cvd::Status::OK);
    response.mutable_command_response();
  } else {
    response.mutable_status()->set_code(cvd::Status::INTERNAL);
    *response.mutable_error_response() = import_res.error().FormatForEnv();
  }
  if (parsed.carryover_client_fd->IsOpen()) {
    auto send_res =
        SendResponse(std::move(parsed.carryover_client_fd), response);
    if (!send_res.ok()) {
      LOG(ERROR) << "Failed to send command response: "
                 << send_res.error().FormatForEnv();
      std::exit(1);
    }
  }
  std::exit(import_res.ok() ? 0 : 1);
}

}  // namespace cuttlefish
