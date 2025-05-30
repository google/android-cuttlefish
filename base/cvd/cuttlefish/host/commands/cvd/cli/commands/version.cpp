/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/version.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fmt/ostream.h>

#include "cuttlefish/common/libs/utils/proto.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/version/version.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Prints version of cvd client and cvd server)";

class CvdVersionHandler : public CvdCommandHandler {
 public:
  CvdVersionHandler() = default;

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));
    fmt::print(std::cout, GetVersionString());
    return {};
  }

  cvd_common::Args CmdList() const override { return {"version"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kSummaryHelpText;
  }
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdVersionHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdVersionHandler());
}

}  // namespace cuttlefish
