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

#include "host/commands/cvd/cli/commands/noop.h"

#include <memory>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Deprecated commands, kept for backward compatibility)";

class CvdNoopHandler : public CvdCommandHandler {
 public:
  Result<void> Handle(const CommandRequest& request) override {
    fmt::print(std::cout, "DEPRECATED: The {} command is a no-op\n",
               request.Subcommand());
    return {};
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

std::unique_ptr<CvdCommandHandler> NewCvdNoopHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdNoopHandler());
}

}  // namespace cuttlefish
