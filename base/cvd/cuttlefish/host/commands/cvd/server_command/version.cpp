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

#include "common/libs/fs/shared_buf.h"
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

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return "version" == invocation.command;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd::Version version;
    version.set_major(cvd::kVersionMajor);
    version.set_minor(cvd::kVersionMinor);
    version.set_build(android::build::GetBuildNumber());
    version.set_crc32(FileCrc(kServerExecPath));
    auto version_str = fmt::format("{}", version);
    auto write_len = WriteAll(request.Out(), version_str);
    CF_EXPECTF(write_len == version_str.size(),
               "Failed to write version output: {}", request.Out()->StrError());
    cvd::Response response;
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  cvd_common::Args CmdList() const override { return {"version"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdVersionHandler() {
  return std::unique_ptr<CvdServerHandler>(new CvdVersionHandler());
}

}  // namespace cuttlefish
