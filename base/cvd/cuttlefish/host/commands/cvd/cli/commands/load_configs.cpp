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
#include "cuttlefish/host/commands/cvd/cli/commands/load_configs.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/command_sequence.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/commands/cvd/utils/interrupt_listener.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

using cvd::config::EnvironmentSpecification;

namespace {

constexpr char kSummaryHelpText[] =
    R"(Loads the given JSON configuration file and launches devices based on the options provided)";

constexpr char kDetailedHelpText[] = R"(
Warning: This command is deprecated, use cvd create --config_file instead.

Usage:
cvd load <config_filepath> [--override=<key>:<value>]

Reads the fields in the JSON configuration file and translates them to corresponding start command and flags.

Optionally fetches remote artifacts prior to launching the cuttlefish environment.

The --override flag can be used to give new values for properties in the config file without needing to edit the file directly.  Convenient for one-off invocations.
)";

Result<LoadFlags> GetLoadFlags(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  auto working_directory = CurrentDirectory();
  return CF_EXPECT(GetFlags(args, working_directory));
}

class LoadConfigsCommand : public CvdCommandHandler {
 public:
  LoadConfigsCommand(CommandSequenceExecutor& executor,
                     InstanceManager& instance_manager)
      : executor_(executor), instance_manager_(instance_manager) {}
  ~LoadConfigsCommand() = default;

  Result<void> Handle(const CommandRequest& request) override {
    bool can_handle_request = CF_EXPECT(CanHandle(request));
    CF_EXPECT_EQ(can_handle_request, true);

    LoadFlags load_flags = CF_EXPECT(GetLoadFlags(request));
    EnvironmentSpecification env_spec =
        CF_EXPECT(GetEnvironmentSpecification(load_flags));

    std::mutex group_creation_mtx;
    // Have to use the group name because LocalInstanceGroup can't be default
    // constructed. A value will be assigned to this variable in the same
    // critical section where the group is created.
    std::string group_name = "";

    auto push_result =
        PushInterruptListener([this, &group_name, &group_creation_mtx](int) {
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
                instance_manager_.FindGroup({.group_name = group_name});
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
    auto group_res = CreateGroup(load_flags.base_dir, env_spec);
    if (group_res.ok()) {
      // Have to initialize the group_name variable before releasing the mutex.
      group_name = (*group_res).GroupName();
    }
    group_creation_mtx.unlock();
    auto group = CF_EXPECT(std::move(group_res));

    auto cvd_flags = CF_EXPECT(ParseCvdConfigs(env_spec, group));

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

    return {};
  }

  Result<void> LoadGroup(const CommandRequest& request,
                         LocalInstanceGroup& group, CvdFlags cvd_flags) {
    auto mkdir_res =
        EnsureDirectoryExists(group.HomeDir(), 0775, /* group_name */ "");
    if (!mkdir_res.ok()) {
      group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
      // TODO: b/471069557 - diagnose unused
      Result<void> unused = instance_manager_.UpdateInstanceGroup(group);
    }
    CF_EXPECT(std::move(mkdir_res));

    if (!cvd_flags.fetch_cvd_flags.empty()) {
      auto fetch_cmd = CF_EXPECT(BuildFetchCmd(request, cvd_flags));
      auto fetch_res = executor_.ExecuteOne(fetch_cmd, std::cerr);
      if (!fetch_res.ok()) {
        group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
        // TODO: b/471069557 - diagnose unused
        Result<void> unused = instance_manager_.UpdateInstanceGroup(group);
      }
      CF_EXPECTF(std::move(fetch_res),
                 "Failed to fetch build artifacts, check '{}' for details",
                 GetFetchLogsFileName(cvd_flags.target_directory));
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
            .AddArguments({"cvd", "fetch"})
            .AddArguments(cvd_flags.fetch_cvd_flags)
            .Build());
  }

  Result<CommandRequest> BuildLaunchCmd(const CommandRequest& request,
                                        const CvdFlags& cvd_flags,
                                        const LocalInstanceGroup& group) {
    auto env = request.Env();
    env["HOME"] = group.HomeDir();
    env[kAndroidHostOut] = group.HostArtifactsPath();
    env[kAndroidSoongHostOut] = group.HostArtifactsPath();

    // Add system flag for multi-build scenario
    std::string system_build_arg =
        fmt::format("--system_image_dir={}", group.ProductOutPath());
    env.erase(kAndroidProductOut);

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
  Result<LocalInstanceGroup> CreateGroup(
      const std::string& base_dir, const EnvironmentSpecification& env_spec) {
    InstanceGroupParams group_params{
        .group_name = env_spec.common().group_name(),
    };
    for (const auto& instance : env_spec.instances()) {
      group_params.instances.emplace_back(InstanceParams{
          .per_instance_name = instance.name(),
      });
    }
    return CF_EXPECT(instance_manager_.CreateInstanceGroup(
        std::move(group_params),
        CF_EXPECT(GetGroupCreationDirectories(base_dir, env_spec))));
  }

  static constexpr char kLoadSubCmd[] = "load";

  CommandSequenceExecutor& executor_;
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewLoadConfigsCommand(
    CommandSequenceExecutor& executor, InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new LoadConfigsCommand(executor, instance_manager));
}

}  // namespace cuttlefish
