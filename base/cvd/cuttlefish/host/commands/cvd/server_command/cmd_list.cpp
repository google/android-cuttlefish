/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/server_command/cmd_list.h"

#include <vector>

#include <android-base/strings.h>
#include <json/value.h>

#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdCmdlistHandler : public CvdServerHandler {
 public:
  CvdCmdlistHandler(CommandSequenceExecutor& executor) : executor_(executor) {}

  Result<bool> CanHandle(const CommandRequest& request) const override {
    auto invocation = ParseInvocation(request);
    return (invocation.command == "cmd-list");
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    cvd::Response response;
    response.mutable_command_response();  // Sets oneof member
    response.mutable_status()->set_code(cvd::Status::OK);

    CF_EXPECT(CanHandle(request));

    auto [subcmd, subcmd_args] = ParseInvocation(request);
    const auto subcmds = executor_.CmdList();

    std::vector<std::string> subcmds_vec{subcmds.begin(), subcmds.end()};
    const auto subcmds_str = android::base::Join(subcmds_vec, ",");
    Json::Value subcmd_info;
    subcmd_info["subcmd"] = subcmds_str;
    std::cout << subcmd_info.toStyledString();
    return response;
  }

  // not intended to be used by the user
  cvd_common::Args CmdList() const override { return {}; }
  // not intended to show up in help
  Result<std::string> SummaryHelp() const override { return ""; }
  bool ShouldInterceptHelp() const override { return false; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return "";
  }

 private:
  CommandSequenceExecutor& executor_;
};

std::unique_ptr<CvdServerHandler> NewCvdCmdlistHandler(
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new CvdCmdlistHandler(executor));
}

}  // namespace cuttlefish
