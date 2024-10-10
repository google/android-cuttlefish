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

#include "host/commands/cvd/command_sequence.h"

#include <memory>

#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "host/commands/cvd/request_context.h"
#include "host/commands/cvd/command_request.h"
#include "host/commands/cvd/types.h"

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
  using android::base::StringReplace;
  return safe ? input : "'" + StringReplace(input, "'", "\\'", true) + "'";
}

std::string FormattedCommand(const CommandRequest& command) {
  std::stringstream effective_command;
  effective_command << "*******************************************************"
                       "*************************\n";
  effective_command << "Executing `";
  for (const auto& [name, val] : command.Env()) {
    effective_command << BashEscape(name) << "=" << BashEscape(val) << " ";
  }
  auto args = command.Args();
  auto selector_args = command.SelectorArgs();
  if (args.empty()) {
    return effective_command.str();
  }
  const auto& cmd = args.front();
  cvd_common::Args cmd_args{args.begin() + 1, args.end()};
  effective_command << BashEscape(cmd) << " ";
  for (const auto& selector_arg : selector_args) {
    effective_command << BashEscape(selector_arg) << " ";
  }
  for (const auto& cmd_arg : cmd_args) {
    effective_command << BashEscape(cmd_arg) << " ";
  }
  effective_command.seekp(-1, effective_command.cur);
  effective_command << "`\n";  // Overwrite last space
  return effective_command.str();
}

}  // namespace

CommandSequenceExecutor::CommandSequenceExecutor(
    const std::vector<std::unique_ptr<CvdServerHandler>>& server_handlers)
    : server_handlers_(server_handlers) {}

Result<std::vector<cvd::Response>> CommandSequenceExecutor::Execute(
    const std::vector<CommandRequest>& requests, std::ostream& report) {
  std::vector<cvd::Response> responses;
  for (const auto& request : requests) {
    report << FormattedCommand(request);

    auto handler = CF_EXPECT(RequestHandler(request, server_handlers_));
    handler_stack_.push_back(handler);
    auto response = CF_EXPECT(handler->Handle(request));
    handler_stack_.pop_back();

    CF_EXPECT(response.status().code() == cvd::Status::OK,
              "Reason: \"" << response.status().message() << "\"");

    responses.emplace_back(std::move(response));
  }
  return {responses};
}

Result<cvd::Response> CommandSequenceExecutor::ExecuteOne(
    const CommandRequest& request, std::ostream& report) {
  auto response_in_vector = CF_EXPECT(Execute({request}, report));
  CF_EXPECT_EQ(response_in_vector.size(), 1ul);
  return response_in_vector.front();
}

std::vector<std::string> CommandSequenceExecutor::CmdList() const {
  std::unordered_set<std::string> subcmds;
  for (const auto& handler : server_handlers_) {
    auto&& cmds_list = handler->CmdList();
    for (const auto& cmd : cmds_list) {
      subcmds.insert(cmd);
    }
  }
  // duplication removed
  return std::vector<std::string>{subcmds.begin(), subcmds.end()};
}

Result<CvdServerHandler*> CommandSequenceExecutor::GetHandler(
    const CommandRequest& request) {
  return CF_EXPECT(RequestHandler(request, server_handlers_));
}

}  // namespace cuttlefish
