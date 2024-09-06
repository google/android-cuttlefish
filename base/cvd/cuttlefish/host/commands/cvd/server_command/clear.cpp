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

#include "host/commands/cvd/server_command/clear.h"

#include <sys/types.h>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>

#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char clearCmd[] = "clear";
constexpr char kSummaryHelpText[] =
    "Clears the instance databaase, stopping any running instances first.";

class CvdClearCommandHandler : public CvdServerHandler {
 public:
  CvdClearCommandHandler(InstanceManager& instance_manager);

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  cvd_common::Args CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  InstanceManager& instance_manager_;
};

CvdClearCommandHandler::CvdClearCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<bool> CvdClearCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return invocation.command == clearCmd;
}

Result<cvd::Response> CvdClearCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));

  cvd::Response response;
  response.mutable_command_response();

  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  if (CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    request.Out() << kSummaryHelpText << std::endl;
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }
  *response.mutable_status() = instance_manager_.CvdClear(request);
  return response;
}

std::vector<std::string> CvdClearCommandHandler::CmdList() const {
  return {clearCmd};
}

Result<std::string> CvdClearCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdClearCommandHandler::ShouldInterceptHelp() const { return false; }

Result<std::string> CvdClearCommandHandler::DetailedHelp(
    std::vector<std::string>& arguments) const {
  return kSummaryHelpText;
}

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdClearCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdClearCommandHandler(instance_manager));
}

}  // namespace cuttlefish

