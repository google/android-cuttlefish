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

#include "host/commands/cvd/server_command/fleet.h"

#include <sys/types.h>

#include <android-base/file.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/status_fetcher.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

constexpr char kSummaryHelpText[] =
    R"(lists active devices with relevant information)";

static constexpr char kHelpMessage[] = R"(
usage: cvd fleet [--help]

  cvd fleet will list the active devices with information.
)";

class CvdFleetCommandHandler : public CvdServerHandler {
 public:
  CvdFleetCommandHandler(InstanceManager& instance_manager,
                         HostToolTargetManager& host_tool_target_manager)
      : instance_manager_(instance_manager),
        status_fetcher_(instance_manager_, host_tool_target_manager) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  cvd_common::Args CmdList() const override { return {kFleetSubcmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kHelpMessage;
  }

 private:
  InstanceManager& instance_manager_;
  StatusFetcher status_fetcher_;

  static constexpr char kFleetSubcmd[] = "fleet";
  Result<cvd::Status> CvdFleetHelp(const SharedFD& out) const;
  bool IsHelp(const cvd_common::Args& cmd_args) const;
};

Result<bool> CvdFleetCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return invocation.command == kFleetSubcmd;
}

Result<cvd::Response> CvdFleetCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));

  cvd::Response ok_response;
  ok_response.mutable_command_response();
  auto& status = *ok_response.mutable_status();
  status.set_code(cvd::Status::OK);

  auto [sub_cmd, args] = ParseInvocation(request.Message());

  if (IsHelp(args)) {
    CF_EXPECT(CvdFleetHelp(request.Out()));
    return ok_response;
  }

  auto all_groups = CF_EXPECT(instance_manager_.FindGroups({}));
  Json::Value groups_json(Json::arrayValue);
  for (const auto& group : all_groups) {
    groups_json.append(
        CF_EXPECT(status_fetcher_.FetchGroupStatus(group, request)));
  }
  Json::Value output_json(Json::objectValue);
  output_json["groups"] = groups_json;
  auto serialized_json = output_json.toStyledString();
  CF_EXPECT_EQ(WriteAll(request.Out(), serialized_json),
               (ssize_t)serialized_json.size());
  return ok_response;
}

bool CvdFleetCommandHandler::IsHelp(const cvd_common::Args& args) const {
  for (const auto& arg : args) {
    if (arg == "--help" || arg == "-help") {
      return true;
    }
  }
  return false;
}

Result<cvd::Status> CvdFleetCommandHandler::CvdFleetHelp(
    const SharedFD& out) const {
  const std::string help_message(kHelpMessage);
  CF_EXPECT_EQ(WriteAll(out, help_message), (ssize_t)help_message.size());
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

std::unique_ptr<CvdServerHandler> NewCvdFleetCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdFleetCommandHandler(instance_manager, host_tool_target_manager));
}

}  // namespace cuttlefish
