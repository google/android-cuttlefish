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

#include <mutex>

#include <android-base/file.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/status_fetcher.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {

static constexpr char kHelpMessage[] = R"(
usage: cvd fleet [--help]

  cvd fleet will list the active devices with information.
)";

class CvdFleetCommandHandler : public CvdServerHandler {
 public:
  CvdFleetCommandHandler(InstanceManager& instance_manager,
                         SubprocessWaiter& subprocess_waiter,
                         HostToolTargetManager& host_tool_target_manager)
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter),
        status_fetcher_(instance_manager_, host_tool_target_manager) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override { return {kFleetSubcmd}; }

 private:
  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  StatusFetcher status_fetcher_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kFleetSubcmd[] = "fleet";
  Result<cvd::Status> CvdFleetHelp(const SharedFD& out) const;
  bool IsHelp(const cvd_common::Args& cmd_args) const;
};

Result<bool> CvdFleetCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return invocation.command == kFleetSubcmd;
}

Result<void> CvdFleetCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<cvd::Response> CvdFleetCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(CanHandle(request));

  cvd::Response ok_response;
  ok_response.mutable_command_response();
  auto& status = *ok_response.mutable_status();
  status.set_code(cvd::Status::OK);

  auto [sub_cmd, args] = ParseInvocation(request.Message());
  auto envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  interrupt_lock.unlock();

  if (IsHelp(args)) {
    CF_EXPECT(CvdFleetHelp(request.Out()));
    return ok_response;
  }

  CF_EXPECT(Contains(envs, "ANDROID_HOST_OUT") &&
            DirectoryExists(envs.at("ANDROID_HOST_OUT")));
  Json::Value groups_json(Json::arrayValue);
  auto all_group_names = instance_manager_.AllGroupNames();
  envs.erase(kCuttlefishInstanceEnvVarName);
  for (const auto& group_name : all_group_names) {
    auto group_obj_copy_result = instance_manager_.SelectGroup(
        {}, InstanceManager::Queries{{selector::kGroupNameField, group_name}},
        {});
    if (!group_obj_copy_result.ok()) {
      LOG(DEBUG) << "Group \"" << group_name
                 << "\" has already been removed. Skipped.";
      continue;
    }

    Json::Value group_json(Json::objectValue);
    group_json["group_name"] = group_name;
    group_json["start_time"] =
        selector::Format(group_obj_copy_result->StartTime());

    auto request_message = MakeRequest(
        {.cmd_args = {"cvd", "status", "--print", "--all_instances"},
         .env = envs,
         .selector_args = {"--group_name", group_name},
         .working_dir =
             request.Message().command_request().working_directory()});
    RequestWithStdio group_request{request.Client(), request_message,
                                   request.FileDescriptors(),
                                   request.Credentials()};
    auto [_, instances_json, group_response] =
        CF_EXPECT(status_fetcher_.FetchStatus(group_request));
    CF_EXPECT_EQ(
        group_response.status().code(), cvd::Status::OK,
        fmt::format(
            "Running cvd status --all_instances for group \"{}\" failed",
            group_name));
    group_json["instances"] = instances_json;
    groups_json.append(group_json);
  }
  Json::Value output_json(Json::objectValue);
  output_json["groups"] = groups_json;
  auto serialized_json = output_json.toStyledString();
  CF_EXPECT_EQ(WriteAll(request.Out(), serialized_json),
               serialized_json.size());
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
  CF_EXPECT_EQ(WriteAll(out, help_message), help_message.size());
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

std::unique_ptr<CvdServerHandler> NewCvdFleetCommandHandler(
    InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(new CvdFleetCommandHandler(
      instance_manager, subprocess_waiter, host_tool_target_manager));
}

}  // namespace cuttlefish
