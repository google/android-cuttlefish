//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/cvd/cli/command_sequence.h"

#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/str_replace.h"

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/request_context.h"

namespace cuttlefish {
namespace {

std::string BashEscape(const std::string& input) {
  bool safe = true;
  for (const auto& c : input) {
    if ('0' <= c && c <= '9') {
      continue;
    }
    if ('a' <= c && c <= 'z') {
      continue;
    }
    if ('A' <= c && c <= 'Z') {
      continue;
    }
    if (c == '_' || c == '-' || c == '.' || c == ',' || c == '/') {
      continue;
    }
    safe = false;
  }
  return safe ? input : "'" + absl::StrReplaceAll(input, {{"'", "\\'"}}) + "'";
}

std::string FormattedCommand(const CommandRequest& command) {
  std::stringstream effective_command;
  effective_command << "*******************************************************"
                       "*************************\n";
  effective_command << "Executing `";
  for (const auto& [name, val] : command.Env()) {
    // Print only those variables that differ from the current environment
    if (StringFromEnv(name) != val) {
      effective_command << BashEscape(name) << "=" << BashEscape(val) << " ";
    }
  }
  effective_command << "cvd " << BashEscape(command.Subcommand()) << " ";
  for (const auto& selector_arg : command.Selectors().AsArgs()) {
    effective_command << BashEscape(selector_arg) << " ";
  }
  for (const auto& cmd_arg : command.SubcommandArguments()) {
    effective_command << BashEscape(cmd_arg) << " ";
  }
  effective_command.seekp(-1, effective_command.cur);
  effective_command << "`\n";  // Overwrite last space
  return effective_command.str();
}

}  // namespace

CommandSequenceExecutor::CommandSequenceExecutor(
    const std::vector<std::unique_ptr<CvdCommandHandler>>& server_handlers)
    : server_handlers_(server_handlers) {}

Result<void> CommandSequenceExecutor::ExecuteOne(
    const CommandRequest& request) {
  std::cerr << FormattedCommand(request);

  auto handler = CF_EXPECT(RequestHandler(request, server_handlers_));
  CF_EXPECT(handler->Handle(request));
  return {};
}

Result<CvdCommandHandler*> CommandSequenceExecutor::GetHandler(
    const CommandRequest& request) {
  return CF_EXPECT(RequestHandler(request, server_handlers_));
}

}  // namespace cuttlefish
