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
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/fetch.h"
#include "cuttlefish/host/commands/cvd/cli/commands/start.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
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

constexpr char kLoadSubCmd[] = "load";

constexpr char kSummaryHelpText[] =
    "Creates and starts an instance group from a JSON configuration file";


Result<CommandRequest> BuildFetchCmd(const CommandRequest& request,
                                     const CvdFlags& cvd_flags) {
  return CF_EXPECT(CommandRequestBuilder()
                       .SetEnv(request.Env())
                       .AddArguments({"cvd", "fetch"})
                       .AddArguments(cvd_flags.fetch_cvd_flags)
                       .Build());
}

Result<CommandRequest> BuildStartCommand(const CommandRequest& request,
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

  selector::SelectorOptions selector_options = cvd_flags.selector_flags;
  selector_options.group_name = group.GroupName();

  return CF_EXPECT(
      CommandRequestBuilder()
          .SetEnv(env)
          /* cvd load will always create instances in daemon mode (to be
           independent of terminal) and will enable reporting automatically
           (to run automatically without question during launch)
           */
          .AddArguments({"cvd", "start", "--daemon", system_build_arg})
          .AddArguments(cvd_flags.launch_cvd_flags)
          .SetSelectorOptions(std::move(selector_options))
          .Build());
}

Result<LocalInstanceGroup> CreateGroup(
    InstanceManager& instance_manager, const std::string& base_dir,
    const EnvironmentSpecification& env_spec) {
  InstanceGroupParams group_params{
      .group_name = env_spec.common().group_name(),
  };
  for (const auto& instance : env_spec.instances()) {
    group_params.instances.emplace_back(InstanceParams{
        .per_instance_name = instance.name(),
    });
  }
  return CF_EXPECT(instance_manager.CreateInstanceGroup(
      std::move(group_params),
      CF_EXPECT(GetGroupCreationDirectories(base_dir, env_spec))));
}

}  // namespace

LoadConfigsCommand::LoadConfigsCommand(InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> LoadConfigsCommand::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  CF_EXPECT(ConsumeFlags(flags, args));
  CF_EXPECT(
      !args.empty(),
      "No arguments provided to cvd command, please provide path to json file");
  std::string& config_path = args.front();

  EnvironmentSpecification env_spec =
      CF_EXPECT(GetEnvironmentSpecification(config_path, flags_.overrides));

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
  auto group_res =
      CreateGroup(instance_manager_, flags_.base_dir, env_spec);
  if (group_res.ok()) {
    // Have to initialize the group_name variable before releasing the mutex.
    group_name = (*group_res).GroupName();
  }
  group_creation_mtx.unlock();
  auto group = CF_EXPECT(std::move(group_res));

  auto cvd_flags = CF_EXPECT(ParseCvdConfigs(env_spec, group));

  auto res = LoadGroup(request, group, std::move(cvd_flags));
  if (!res.ok()) {
    auto first_instance_state = group.Instances()[0].State();
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

Result<void> LoadConfigsCommand::LoadGroup(const CommandRequest& request,
                                           LocalInstanceGroup& group,
                                           CvdFlags cvd_flags) {
  auto mkdir_res =
      EnsureDirectoryExists(group.HomeDir(), 0775, /* group_name */ "");
  if (!mkdir_res.ok()) {
    group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
    // TODO: b/471069557 - diagnose unused
    Result<void> unused = instance_manager_.UpdateInstanceGroup(group);
  }
  CF_EXPECT(std::move(mkdir_res));

  if (!cvd_flags.fetch_cvd_flags.empty()) {
    CommandRequest fetch_cmd = CF_EXPECT(BuildFetchCmd(request, cvd_flags));
    std::unique_ptr<CvdCommandHandler> fetch_handler =
        NewCvdFetchCommandHandler();
    Result<void> fetch_res = fetch_handler->Handle(fetch_cmd);
    if (!fetch_res.ok()) {
      group.SetAllStates(cvd::INSTANCE_STATE_PREPARE_FAILED);
      // TODO: b/471069557 - diagnose unused
      Result<void> unused = instance_manager_.UpdateInstanceGroup(group);
    }
    CF_EXPECTF(std::move(fetch_res),
               "Failed to fetch build artifacts, check '{}' for details",
               GetFetchLogsFileName(cvd_flags.target_directory));
  }

  // Instances go from preparing to stopped state after fetching is done.
  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));

  CommandRequest start_cmd =
      CF_EXPECT(BuildStartCommand(request, cvd_flags, group));
  std::unique_ptr<CvdCommandHandler> start_handler =
      NewCvdStartCommandHandler(instance_manager_);
  CF_EXPECT(start_handler->Handle(start_cmd));
  return {};
}

cvd_common::Args LoadConfigsCommand::CmdList() const { return {kLoadSubCmd}; }

std::string LoadConfigsCommand::SummaryHelp() const { return kSummaryHelpText; }

std::vector<HelpParagraph> LoadConfigsCommand::Description() const {
  std::vector<HelpParagraph> description;
  description.emplace_back(
      "This command is an alias of `cvd create --config_file=<config_filepath> "
      "[--override=<key>:<value>]...`, provided for convenience and backwards "
      "compatibility.");

  description.emplace_back("Usage:");

  description.emplace_back(
      "    cvd load <config_filepath> [--override=<key>:<value>]");
  std::vector<HelpParagraph> common_description = CommonCommandDescription();
  description.insert(description.end(), common_description.begin(),
                     common_description.end());
  return description;
}

std::vector<HelpParagraph> LoadConfigsCommand::CommonCommandDescription() {
  std::vector<HelpParagraph> description;
  description.emplace_back(
      "Creates and starts a new instance group from a specification file. An "
      "example specification file looks like:");

  description.emplace_back(HelpParagraph::Raw(R"(  {
    "instances": [
      {
        "name": "ins-1",
        "disk": {
          "default_build": "@ab/aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug"
        },
        "vm": {
          "cpus": 8,
          "memory_mb": 2048
        }
      },
      {
        "name": "ins-2",
        "disk": {
          "default_build": "/path/to/android/build"
        }
      }
    ]
  })"));

  description.emplace_back(
      "A complete reference of the specification file format can be found in "
      "https://github.com/google/android-cuttlefish/blob/main/base/cvd/"
      "cuttlefish/host/commands/cvd/cli/parser/load_config.proto.");

  description.emplace_back(
      "While most config file properties are self explanatory, the build "
      "properties (default_build, kernel.build, bootloader.build, etc) require "
      "more explanation. These properties support two types of values:");

  description.emplace_back(HelpParagraph::Raw(
      R"( - "@ab/<branch_or_build_id>[/<target>[{<filepath>}]]"
 - "<absolute_path>")"));

  description.emplace_back(
      "If the build value starts with \"@ab\", cvd will fetch the specified "
      "Android build target from the Android build servers. By default it will "
      "download the cuttlefish host package archive or the images zip as "
      "needed, but for more advanced use cases the file to download from the "
      "server can be specified with the <filepath> optional parameter in curly "
      "braces. For more information on build fetching and caching operations "
      "refer to `cvd help fetch`.");

  description.emplace_back(
      "Alternatively, the build value may point to an absolute path (starts "
      "with '/') in the filesystem where the Android source code has been "
      "checked out and a Cuttlefish target has been built. This is "
      "particularly useful for rapid iteration during development in "
      "combination with incremental builds and the `cvd stop` and `cvd start` "
      "subcommands.");
  return description;
}

Result<std::vector<Flag>> LoadConfigsCommand::Flags(
    const CommandRequest& request) {
  return BuildCvdLoadFlags(flags_);
}

std::unique_ptr<CvdCommandHandler> NewLoadConfigsCommand(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new LoadConfigsCommand(instance_manager));
}

}  // namespace cuttlefish
