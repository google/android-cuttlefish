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
#include "host/commands/cvd/server_command/load_configs.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/interrupt_listener.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Loads the given JSON configuration file and launches devices based on the options provided)";

constexpr char kDetailedHelpText[] = R"(
Warning: This command is deprecated, use cvd start --config_file instead.

Usage:
cvd load <config_filepath> [--override=<key>:<value>]

Reads the fields in the JSON configuration file and translates them to corresponding start command and flags.  

Optionally fetches remote artifacts prior to launching the cuttlefish environment.

The --override flag can be used to give new values for properties in the config file without needing to edit the file directly.  Convenient for one-off invocations.
)";

Result<CvdFlags> GetCvdFlags(const RequestWithStdio& request) {
  auto args = ParseInvocation(request.Message()).arguments;
  auto working_directory =
      request.Message().command_request().working_directory();
  const LoadFlags flags = CF_EXPECT(GetFlags(args, working_directory));
  return CF_EXPECT(GetCvdFlags(flags));
}

}  // namespace

class LoadConfigsCommand : public CvdServerHandler {
 public:
  LoadConfigsCommand(CommandSequenceExecutor& executor,
                     InstanceManager& instance_manager)
      : executor_(executor), instance_manager_(instance_manager) {}
  ~LoadConfigsCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == kLoadSubCmd;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    bool can_handle_request = CF_EXPECT(CanHandle(request));
    CF_EXPECT_EQ(can_handle_request, true);

    auto cvd_flags = CF_EXPECT(GetCvdFlags(request));
    std::string group_home_directory =
        cvd_flags.load_directories.launch_home_directory;

    std::mutex group_creation_mtx;

    auto push_result =
        PushInterruptListener([this, &group_home_directory, &group_creation_mtx](int) {
          // Creating the listener before the group exists has a very low chance
          // that it may run before the group is actually created and fail,
          // that's fine. The alternative is having a very low chance of being
          // interrupted before the listener is setup and leaving the group in
          // the wrong state in the database.
          LOG(ERROR) << "Interrupt signal received";
          // There is a race here if the signal arrived just before the
          // subprocess was created. Hopefully, by aborting fast the
          // cvd_internal_start subprocess won't have time to complete and
          // receive the SIGHUP signal, so nothing should be left behind.
          {
            std::lock_guard lock(group_creation_mtx);
            auto group_res = instance_manager_.FindGroup(
                selector::Query(selector::kHomeField, group_home_directory));
            if (!group_res.ok()) {
              LOG(ERROR) << "Failed to load group from database: "
                         << group_res.error().Message();
              // Abort while holding the lock to prevent the group from being
              // created if it didn't exist yet
              std::abort();
            }
            auto& group = *group_res;
            group.SetAllStates(cvd::INSTANCE_STATE_CANCELLED);
            auto update_res = instance_manager_.UpdateInstanceGroup(group);
            if (!update_res.ok()) {
              LOG(ERROR) << "Failed to update groups status: "
                         << update_res.error().Message();
            }
            std::abort();
          }
        });
    auto listener_handle = CF_EXPECT(std::move(push_result));

    group_creation_mtx.lock();
    // Don't use CF_EXPECT here or the mutex will be left locked.
    auto group_res = CreateGroup(cvd_flags);
    group_creation_mtx.unlock();
    auto group = CF_EXPECT(std::move(group_res));

    auto res = LoadGroup(request, group, std::move(cvd_flags));
    if (!res.ok()) {
      auto first_instance_state = group.Instances()[0].state();
      // The failure could have occurred during prepare(fetch) or start
      auto failed_state = first_instance_state == cvd::INSTANCE_STATE_PREPARING
                              ? cvd::INSTANCE_STATE_PREPARE_FAILED
                              : cvd::INSTANCE_STATE_BOOT_FAILED;
      group.SetAllStates(failed_state);
      CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
      CF_EXPECT(std::move(res));
    }
    listener_handle.reset();

    cvd::Response response;
    response.mutable_command_response();
    return response;
  }

  Result<void> LoadGroup(const RequestWithStdio& request,
                         selector::LocalInstanceGroup& group,
                         CvdFlags cvd_flags) {
    auto mkdir_cmd = BuildMkdirCmd(request, cvd_flags);
    auto mkdir_res = executor_.ExecuteOne(mkdir_cmd, request.Err());
    if (!mkdir_res.ok()) {
      group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
      instance_manager_.UpdateInstanceGroup(group);
    }
    CF_EXPECT(std::move(mkdir_res));

    if (!cvd_flags.fetch_cvd_flags.empty()) {
      auto fetch_cmd = BuildFetchCmd(request, cvd_flags);
      auto fetch_res = executor_.ExecuteOne(fetch_cmd, request.Err());
      if (!fetch_res.ok()) {
        group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
        instance_manager_.UpdateInstanceGroup(group);
      }
      CF_EXPECT(std::move(fetch_res));
    }

    auto launch_cmd = BuildLaunchCmd(request, cvd_flags, group);
    CF_EXPECT(executor_.ExecuteOne(launch_cmd, request.Err()));
    return {};
  }

  cvd_common::Args CmdList() const override { return {kLoadSubCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

  RequestWithStdio BuildFetchCmd(const RequestWithStdio& request,
                                 const CvdFlags& cvd_flags) {
    cvd::Request fetch_req;
    auto& fetch_cmd = *fetch_req.mutable_command_request();
    *fetch_cmd.mutable_env() = request.Message().command_request().env();
    fetch_cmd.add_args("cvd");
    fetch_cmd.add_args("fetch");
    for (const auto& flag : cvd_flags.fetch_cvd_flags) {
      fetch_cmd.add_args(flag);
    }
    return RequestWithStdio(fetch_req,
                            {request.In(), request.Out(), request.Err()});
  }

  RequestWithStdio BuildMkdirCmd(const RequestWithStdio& request,
                                 const CvdFlags& cvd_flags) {
    cvd::Request mkdir_req;
    auto& mkdir_cmd = *mkdir_req.mutable_command_request();
    *mkdir_cmd.mutable_env() = request.Message().command_request().env();
    mkdir_cmd.add_args("cvd");
    mkdir_cmd.add_args("mkdir");
    mkdir_cmd.add_args("-p");
    mkdir_cmd.add_args(cvd_flags.load_directories.launch_home_directory);
    return RequestWithStdio(mkdir_req,
                            {request.In(), request.Out(), request.Err()});
  }

  RequestWithStdio BuildLaunchCmd(const RequestWithStdio& request,
                                  const CvdFlags& cvd_flags,
                                  const selector::LocalInstanceGroup& group) {
    cvd::Request launch_req;
    auto& launch_cmd = *launch_req.mutable_command_request();
    launch_cmd.set_working_directory(
        cvd_flags.load_directories.host_package_directory);
    *launch_cmd.mutable_env() = request.Message().command_request().env();
    (*launch_cmd.mutable_env())["HOME"] =
        cvd_flags.load_directories.launch_home_directory;
    (*launch_cmd.mutable_env())[kAndroidHostOut] =
        cvd_flags.load_directories.host_package_directory;
    (*launch_cmd.mutable_env())[kAndroidSoongHostOut] =
        cvd_flags.load_directories.host_package_directory;
    if (Contains(*launch_cmd.mutable_env(), kAndroidProductOut)) {
      (*launch_cmd.mutable_env()).erase(kAndroidProductOut);
    }

    /* cvd load will always create instances in daemon mode (to be independent
     of terminal) and will enable reporting automatically (to run automatically
     without question during launch)
     */
    launch_cmd.add_args("cvd");
    launch_cmd.add_args("start");
    launch_cmd.add_args("--daemon");

    for (const auto& parsed_flag : cvd_flags.launch_cvd_flags) {
      launch_cmd.add_args(parsed_flag);
    }
    // Add system flag for multi-build scenario
    launch_cmd.add_args(cvd_flags.load_directories.system_image_directory_flag);

    auto selector_opts = launch_cmd.mutable_selector_opts();

    for (const auto& flag : cvd_flags.selector_flags) {
      selector_opts->add_args(flag);
    }

    // Make sure the newly created group is used by cvd start
    launch_cmd.mutable_selector_opts()->add_args("--group_name");
    launch_cmd.mutable_selector_opts()->add_args(group.GroupName());

    return RequestWithStdio(launch_req,
                            {request.In(), request.Out(), request.Err()});
  }

 private:
  Result<selector::LocalInstanceGroup> CreateGroup(const CvdFlags& cvd_flags) {
    selector::GroupCreationInfo group_info{
        .home = cvd_flags.load_directories.launch_home_directory,
        .host_artifacts_path = cvd_flags.load_directories.host_package_directory,
        .group_name = cvd_flags.group_name? *cvd_flags.group_name: "",
    };
    for (const auto& instance_name : cvd_flags.instance_names) {
      group_info.instances.emplace_back(0, instance_name,
                                        cvd::INSTANCE_STATE_PREPARING);
    }
    return CF_EXPECT(instance_manager_.CreateInstanceGroup(group_info));
  }

  static constexpr char kLoadSubCmd[] = "load";

  CommandSequenceExecutor& executor_;
  InstanceManager& instance_manager_;
};

std::unique_ptr<CvdServerHandler> NewLoadConfigsCommand(
    CommandSequenceExecutor& executor, InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new LoadConfigsCommand(executor, instance_manager));
}

}  // namespace cuttlefish
