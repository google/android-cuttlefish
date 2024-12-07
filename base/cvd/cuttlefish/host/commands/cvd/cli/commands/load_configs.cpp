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
#include "host/commands/cvd/cli/commands/load_configs.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/command_sequence.h"
#include "host/commands/cvd/cli/parser/load_configs_parser.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/utils/common.h"
#include "host/commands/cvd/utils/interrupt_listener.h"

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

Result<CvdFlags> GetCvdFlags(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  auto working_directory = CurrentDirectory();
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

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    bool can_handle_request = CF_EXPECT(CanHandle(request));
    CF_EXPECT_EQ(can_handle_request, true);

    auto cvd_flags = CF_EXPECT(GetCvdFlags(request));
    std::string group_home_directory =
        cvd_flags.load_directories.launch_home_directory;

    std::mutex group_creation_mtx;

    auto push_result = PushInterruptListener(
        [this, &group_home_directory, &group_creation_mtx](int) {
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
            auto group_res =
                instance_manager_.FindGroup({.home = group_home_directory});
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

  Result<void> LoadGroup(const CommandRequest& request,
                         LocalInstanceGroup& group, CvdFlags cvd_flags) {
    auto mkdir_res =
        EnsureDirectoryExists(cvd_flags.load_directories.launch_home_directory,
                              0775, /* group_name */ "");
    if (!mkdir_res.ok()) {
      group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
      instance_manager_.UpdateInstanceGroup(group);
    }
    CF_EXPECT(std::move(mkdir_res));

    if (!cvd_flags.fetch_cvd_flags.empty()) {
      auto fetch_cmd = CF_EXPECT(BuildFetchCmd(request, cvd_flags));
      auto fetch_res = executor_.ExecuteOne(fetch_cmd, std::cerr);
      if (!fetch_res.ok()) {
        group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
        instance_manager_.UpdateInstanceGroup(group);
      }
      CF_EXPECTF(
          std::move(fetch_res),
          "Failed to fetch build artifacts, check {} for details",
          GetFetchLogsFileName(cvd_flags.load_directories.target_directory));
    }

    auto launch_cmd = CF_EXPECT(BuildLaunchCmd(request, cvd_flags, group));
    CF_EXPECT(executor_.ExecuteOne(launch_cmd, std::cerr));
    return {};
  }

  cvd_common::Args CmdList() const override { return {kLoadSubCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

  Result<CommandRequest> BuildFetchCmd(const CommandRequest& request,
                                       const CvdFlags& cvd_flags) {
    return CF_EXPECT(
        CommandRequestBuilder()
            .SetEnv(request.Env())
            // The fetch operation is too verbose by default, set it to WARNING
            // unconditionally, the full logs are available in fetch.log
            // anyways.
            .AddArguments({"cvd", "fetch", "-verbosity", "WARNING"})
            .AddArguments(cvd_flags.fetch_cvd_flags)
            .Build());
  }

  Result<CommandRequest> BuildLaunchCmd(const CommandRequest& request,
                                        const CvdFlags& cvd_flags,
                                        const LocalInstanceGroup& group) {
    // Add system flag for multi-build scenario
    std::string system_build_arg = fmt::format(
        "--system_image_dir={}",
        cvd_flags.load_directories.system_image_directory_flag_value);
    auto env = request.Env();
    env["HOME"] = cvd_flags.load_directories.launch_home_directory;
    env[kAndroidHostOut] = cvd_flags.load_directories.host_package_directory;
    env[kAndroidSoongHostOut] =
        cvd_flags.load_directories.host_package_directory;
    if (Contains(env, kAndroidProductOut)) {
      env.erase(kAndroidProductOut);
    }

    return CF_EXPECT(
        CommandRequestBuilder()
            .SetEnv(env)
            // The newly created instances don't have an id yet, create will
            // allocate those.
            /* cvd load will always create instances in daemon mode (to be
             independent of terminal) and will enable reporting automatically
             (to run automatically without question during launch)
             */
            .AddArguments({"cvd", "create", "--daemon", system_build_arg})
            .AddArguments(cvd_flags.launch_cvd_flags)
            .AddSelectorArguments(cvd_flags.selector_flags)
            .AddSelectorArguments({"--group_name", group.GroupName()})
            .Build());
  }

 private:
  Result<LocalInstanceGroup> CreateGroup(const CvdFlags& cvd_flags) {
    selector::GroupCreationInfo group_info{
        .home = cvd_flags.load_directories.launch_home_directory,
        .host_artifacts_path =
            cvd_flags.load_directories.host_package_directory,
        .product_out_path =
            cvd_flags.load_directories.system_image_directory_flag_value,
        .group_name = cvd_flags.group_name ? *cvd_flags.group_name : "",
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
