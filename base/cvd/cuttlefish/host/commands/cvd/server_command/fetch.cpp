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

#include "host/commands/cvd/server_command/fetch.h"

#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdFetchCommandHandler : public CvdServerHandler {
 public:
  CvdFetchCommandHandler()
      : fetch_cmd_list_{std::vector<std::string>{"fetch", "fetch_cvd"}} {}

  Result<bool> CanHandle(const CommandRequest& request) const override;
  Result<cvd::Response> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override { return fetch_cmd_list_; }
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override { return true; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  std::vector<std::string> fetch_cmd_list_;
};

Result<bool> CvdFetchCommandHandler::CanHandle(
    const CommandRequest& request) const {
  auto invocation = ParseInvocation(request);
  return Contains(fetch_cmd_list_, invocation.command);
}

Result<cvd::Response> CvdFetchCommandHandler::Handle(
    const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));

  std::vector<std::string> args;
  args.emplace_back("fetch_cvd");

  for (const auto& argument : ParseInvocation(request).arguments) {
    args.emplace_back(argument);
  }

  std::vector<char*> args_data;
  for (auto& argument : args) {
    args_data.emplace_back(argument.data());
  }

  CF_EXPECT(FetchCvdMain(args_data.size(), args_data.data()));

  cvd::Response response;
  response.mutable_command_response();
  response.mutable_status()->set_code(cvd::Status::OK);
  return response;
}

Result<std::string> CvdFetchCommandHandler::SummaryHelp() const {
  return "Retrieve build artifacts based on branch and target names";
}

Result<std::string> CvdFetchCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  Command fetch_command("/proc/self/exe");
  fetch_command.SetName("fetch_cvd");
  fetch_command.SetExecutable("/proc/self/exe");
  fetch_command.AddParameter("--help");

  std::string output;
  RunWithManagedStdio(std::move(fetch_command), nullptr, nullptr, &output);
  return output;
}

std::unique_ptr<CvdServerHandler> NewCvdFetchCommandHandler() {
  return std::unique_ptr<CvdServerHandler>(new CvdFetchCommandHandler());
}

}  // namespace cuttlefish
