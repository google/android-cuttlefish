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

#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_client.h"

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

std::string FormattedCommand(const cvd::CommandRequest command) {
  std::stringstream effective_command;
  effective_command << "Executing `";
  for (const auto& [name, val] : command.env()) {
    effective_command << BashEscape(name) << "=" << BashEscape(val) << " ";
  }
  for (const auto& argument : command.args()) {
    effective_command << BashEscape(argument) << " ";
  }
  effective_command.seekp(-1, effective_command.cur);
  effective_command << "`\n";  // Overwrite last space
  return effective_command.str();
}

}  // namespace

CommandSequenceExecutor::CommandSequenceExecutor() {}

Result<void> CommandSequenceExecutor::LateInject(fruit::Injector<>& injector) {
  server_handlers_ = injector.getMultibindings<CvdServerHandler>();
  return {};
}

Result<void> CommandSequenceExecutor::Interrupt() {
  std::unique_lock interrupt_lock(interrupt_mutex_);
  interrupted_ = true;
  if (handler_stack_.empty()) {
    return {};
  }
  CF_EXPECT(handler_stack_.back()->Interrupt());
  return {};
}

Result<std::vector<cvd::Response>> CommandSequenceExecutor::Execute(
    const std::vector<RequestWithStdio>& requests, SharedFD report) {
  std::unique_lock interrupt_lock(interrupt_mutex_);
  CF_EXPECT(!interrupted_, "Interrupted");

  std::vector<cvd::Response> responses;
  for (const auto& request : requests) {
    auto& inner_proto = request.Message();
    if (inner_proto.has_command_request()) {
      auto& command = inner_proto.command_request();
      std::string str = FormattedCommand(command);
      CF_EXPECT(WriteAll(report, str) == str.size(), report->StrError());
    }

    auto handler = CF_EXPECT(RequestHandler(request, server_handlers_));
    handler_stack_.push_back(handler);
    interrupt_lock.unlock();
    auto response = CF_EXPECT(handler->Handle(request));
    interrupt_lock.lock();
    handler_stack_.pop_back();

    CF_EXPECT(interrupted_ == false, "Interrupted");
    CF_EXPECT(response.status().code() == cvd::Status::OK,
              "Reason: \"" << response.status().message() << "\"");

    static const char kDoneMsg[] = "Done\n";
    CF_EXPECT(WriteAll(request.Err(), kDoneMsg) == sizeof(kDoneMsg) - 1,
              request.Err()->StrError());
    responses.emplace_back(std::move(response));
  }
  return {responses};
}

fruit::Component<CommandSequenceExecutor> CommandSequenceExecutorComponent() {
  return fruit::createComponent()
      .addMultibinding<LateInjected, CommandSequenceExecutor>();
}

}  // namespace cuttlefish
