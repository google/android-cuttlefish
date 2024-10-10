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

#include "host/commands/cvd/server_command/version.h"

#include "build/version.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"

#include "common/libs/utils/proto.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/server_constants.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/host_tools_version.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Prints version of cvd client and cvd server)";

class CvdVersionHandler : public CvdServerHandler {
 public:
  CvdVersionHandler() = default;

  Result<bool> CanHandle(const CommandRequest& request) const override {
    auto invocation = ParseInvocation(request);
    return "version" == invocation.command;
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    fmt::print(std::cout, "major: {}\n", cvd::kVersionMajor);
    fmt::print(std::cout, "minor: {}\n", cvd::kVersionMinor);

    std::string build = android::build::GetBuildNumber();
    if (!build.empty()) {
      fmt::print(std::cout, "build: {}\n", build);
    }
    fmt::print(std::cout, "crc32: {}\n", FileCrc(kServerExecPath));

    cvd::Response response;
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  cvd_common::Args CmdList() const override { return {"version"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kSummaryHelpText;
  }
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdVersionHandler() {
  return std::unique_ptr<CvdServerHandler>(new CvdVersionHandler());
}

}  // namespace cuttlefish
