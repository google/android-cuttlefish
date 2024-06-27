/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/server_command/noop.h"

#include <memory>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/utils.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Deprecated commands, kept for backward compatibility)";

class CvdNoopHandler : public CvdServerHandler {
 public:
  CvdNoopHandler() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return Contains(CmdList(), invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    auto invocation = ParseInvocation(request.Message());
    auto msg = fmt::format("DEPRECATED: The {} command is a no-op",
                           invocation.command);
    auto write_len = WriteAll(request.Out(), msg);
    CF_EXPECTF(write_len == (ssize_t)msg.size(),
               "Failed to write deprecation message: {}",
               request.Out()->StrError());
    cvd::Response response;
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  cvd_common::Args CmdList() const override {
    return cvd_common::Args{"server-kill", "kill-server", "restart-server"};
  }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return "DEPRECATED: This command is a no-op";
  }
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdNoopHandler() {
  return std::unique_ptr<CvdServerHandler>(new CvdNoopHandler());
}

}  // namespace cuttlefish
