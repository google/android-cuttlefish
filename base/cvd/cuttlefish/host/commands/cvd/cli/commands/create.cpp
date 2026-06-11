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

#include "cuttlefish/host/commands/cvd/cli/commands/create.h"

#include <errno.h>
#include <stddef.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/cli/commands/load_configs.h"
#include "cuttlefish/host/commands/cvd/cli/commands/start.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/creation_analyzer.h"
#include "cuttlefish/host/commands/cvd/cli/selector/num_instances_parser.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_constants.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using selector::AnalyzeCreation;
using selector::GroupCreationInfo;

constexpr char kSummaryHelpText[] =
    "Create a Cuttlefish instance group";



std::string DefaultHostPath(const cvd_common::Envs& envs) {
  for (const auto& key : {kAndroidHostOut, kAndroidSoongHostOut, "HOME"}) {
    auto it = envs.find(key);
    if (it != envs.end()) {
      return it->second;
    }
  }
  return CurrentDirectory();
}

std::string DefaultProductPath(const cvd_common::Envs& envs) {
  for (const auto& key : {kAndroidProductOut, "HOME"}) {
    auto it = envs.find(key);
    if (it != envs.end()) {
      return it->second;
    }
  }
  return CurrentDirectory();
}


Result<CommandRequest> CreateLoadCommand(const CommandRequest& request,
                                         cvd_common::Args& args,
                                         const std::string& config_file) {
  return CF_EXPECT(CommandRequestBuilder()
                       .SetEnv(request.Env())
                       .AddArguments({"cvd", "load"})
                       .AddArguments(args)
                       .AddArguments({config_file})
                       .Build());
}

Result<CommandRequest> CreateStartCommand(const LocalInstanceGroup& group,
                                          const cvd_common::Args& args,
                                          const cvd_common::Envs& envs) {
  selector::SelectorOptions selector_options{
      .group_name = group.GroupName(),
  };
  return CF_EXPECT(CommandRequestBuilder()
                       .SetEnv(envs)
                       .AddArguments({"cvd", "start"})
                       .AddArguments(args)
                       .SetSelectorOptions(std::move(selector_options))
                       .Build());
}

Result<cvd_common::Envs> GetEnvs(const CommandRequest& request) {
  cvd_common::Envs envs = request.Env();
  if (auto it = envs.find("HOME"); it != envs.end() && it->second.empty()) {
    envs.erase(it);
  }
  if (Contains(envs, "HOME")) {
    // As the end-user may override HOME, this could be a relative path
    // to client's pwd, or may include "~" which is the client's actual
    // home directory.
    const auto given_home_dir = envs["HOME"];
    // Substituting ~ is not supported by cvd
    CF_EXPECT(!absl::StartsWith(given_home_dir, "~") &&
                  !absl::StartsWith(given_home_dir, "~/"),
              "The HOME directory should not start with ~");
    envs["HOME"] = AbsolutePath(given_home_dir);
  }
  return envs;
}

// link might be a directory, so we clean that up, and create a link from
// target to link
Result<void> EnsureSymlink(const std::string& target, const std::string link) {
  if (DirectoryExists(link, /* follow_symlinks */ false)) {
    CF_EXPECTF(RecursivelyRemoveDirectory(link),
               "Failed to remove legacy directory \"{}\"", link);
  }
  if (FileExists(link, /* follow_symlinks */ false)) {
    CF_EXPECTF(RemoveFile(link), "Failed to remove file \"{}\": {}", link,
               StrError(errno));
  }
  CF_EXPECT(Symlink(target, link));
  return {};
}

Result<bool> IsDefaultGroup(const CommandRequest& request) {
  return StringFromEnv("HOME", "") == CF_EXPECT(SystemWideUserHome()) &&
         !request.Selectors().HasOptions();
}

// For backward compatibility, we add extra symlink in home dir
Result<void> CreateSymlinks(const LocalInstanceGroup& group) {
  auto system_wide_home = CF_EXPECT(SystemWideUserHome());
  CF_EXPECT(EnsureDirectoryExists(group.HomeDir()));
  auto smallest_id = std::numeric_limits<unsigned>::max();
  for (const auto& instance : group.Instances()) {
    // later on, we link cuttlefish_runtime to cuttlefish_runtime.smallest_id
    smallest_id = std::min(smallest_id, instance.Id());
    std::string instance_home_dir = fmt::format(
        "{}/cuttlefish/instances/cvd-{}", group.HomeDir(), instance.Id());
    if (!FileExists(instance_home_dir)) {
      // Legacy launchers create cuttlefish_runtime.{$ID}
      instance_home_dir = fmt::format("{}/cuttlefish_runtime.{}",
                                      group.HomeDir(), instance.Id());
    }
    CF_EXPECT(EnsureSymlink(instance_home_dir,
                            fmt::format("{}/cuttlefish_runtime.{}",
                                        system_wide_home, instance.Id())));
    std::string cuttlefish_home_dir = group.HomeDir() + "/cuttlefish";
    if (FileExists(cuttlefish_home_dir)) {
      // Legacy Cuttlefish launchers don't create this directory so no need to
      // create a corresponding symlink in that case.
      CF_EXPECT(
          EnsureSymlink(cuttlefish_home_dir, system_wide_home + "/cuttlefish"));
    }
  }
  // create cuttlefish_runtime to cuttlefish_runtime.id
  CF_EXPECT_NE(std::numeric_limits<unsigned>::max(), smallest_id,
               "The group did not have any instance, which is not expected.");

  // The config file needs to be copied instead of symlinked because when the
  // group is removed the original file will be deleted leaving the symlink
  // dangling. The config file in the home directory is used by
  // cvd_internal_start to persist the user's choice for
  // -report_anonymous_usage_stats.
  CF_EXPECT(
      Copy(group.Instances()[0].InstanceDirectory() + "/cuttlefish_config.json",
           CF_EXPECT(SystemWideUserHome()) + "/.cuttlefish_config.json"),
      "Failed to copy config file to home directory");

  const std::string instance_runtime_dir =
      fmt::format("{}/cuttlefish_runtime.{}", system_wide_home, smallest_id);
  const std::string runtime_dir_link = system_wide_home + "/cuttlefish_runtime";
  CF_EXPECT(EnsureSymlink(instance_runtime_dir, runtime_dir_link));
  return {};
}

// The create subcommand uses the selector flags in a different way than other
// subcommands, so the default help message is not appropriate. Ideally, create
// should just use different flag objects with the same name, but selector flags
// are parsed before calling the handler's functions so this is not possible.
// The other option would be to use differently named flags altogether, but that
// would break backwards compatibility at this point.
// This function produces the same flags with different help message.
std::vector<Flag> BuildSelectorFlagsForCreateHelp(
    const selector::SelectorOptions& selectors) {
  return {
      Flag::StringFlag(selector::SelectorFlags::kGroupName)
          .Getter([selectors]() { return selectors.group_name.value_or(""); })
          .Help("Name of the group to be created. The command will fail if a "
                "group with that name already exists. A new name of the form "
                "'cvd-<n>' guaranteed to not exist yet will be generated if "
                "not provided."),
      Flag::StringFlag(selector::SelectorFlags::kInstanceName)
          .Getter([selectors]() -> std::string {
            return absl::StrJoin(
                selectors.instance_names.value_or(std::vector<std::string>()),
                ",");
          })
          .ValueNameHint("NAME[,NAME...]")
          .Help("Comma separated list of instance names. When provided, this "
                "flag determines the number of instaces to have in the group, "
                "so it must match the --num_instances flag. Valid names will "
                "be generated automaticlally if no value is provided. "
                "Instances in the same group must have different names, but "
                "can share names with instances from other groups"),
  };
}

}  // namespace

CvdCreateCommandHandler::CvdCreateCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdCreateCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> subcmd_args = request.SubcommandArguments();

  cvd_common::Envs envs = CF_EXPECT(GetEnvs(request));

  CF_EXPECT(ConsumeFlags(ConfigFileModeFlags(), subcmd_args));

  if (!own_flags_.config_file.empty()) {
    CommandRequest subrequest = CF_EXPECT(
        CreateLoadCommand(request, subcmd_args, own_flags_.config_file));
    std::unique_ptr<CvdCommandHandler> load_handler =
        NewLoadConfigsCommand(instance_manager_);
    CF_EXPECT(load_handler->Handle(subrequest));
    return {};
  }

  CF_EXPECT(ConsumeFlags(FlagModeFlags(request.Env(), request.Selectors()),
                         subcmd_args));

  // Validate the host artifacts path before proceeding
  (void)CF_EXPECT(
      HostToolTarget(own_flags_.host_path).GetStartBinName(),
      "\nCould not find the required host tools to launch a device.\n\n"
      "If you already have the host tools and devices images downloaded use "
      "the `--host_path` and `--product_path` flags.\nSee `cvd help create` "
      "for more details.\n\n"
      "If you need to download host tools or system images try using `cvd "
      "fetch`.\nFor example: `cvd fetch --default_build=<branch>/<target>`\n\n"
      "If you are building Android from source, try running `lunch <target>; "
      "m` to set up your environment and build the images.");
  // CreationAnalyzer needs these to be set in the environment
  envs[kAndroidHostOut] = AbsolutePath(own_flags_.host_path);
  envs[kAndroidProductOut] = AbsolutePath(own_flags_.product_path);
  auto group =
      CF_EXPECT(CreateGroup(instance_manager_, subcmd_args, envs, request));

  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));

  if (own_flags_.start) {
    CommandRequest start_cmd =
        CF_EXPECT(CreateStartCommand(group, subcmd_args, envs));
    std::unique_ptr<CvdCommandHandler> start_handler =
        NewCvdStartCommandHandler(instance_manager_);
    CF_EXPECT(start_handler->Handle(start_cmd));

    if (CF_EXPECT(IsDefaultGroup(request))) {
      // For backward compatibility, we add extra symlink in system wide home
      // when HOME is NOT overridden and selector flags are NOT given.
      auto symlink_res = CreateSymlinks(group);
      if (!symlink_res.ok()) {
        LOG(ERROR) << "Failed to create symlinks for default group: "
                   << symlink_res.error();
      }
    }
  }

  return {};
}

Result<LocalInstanceGroup> CvdCreateCommandHandler::CreateGroup(
    InstanceManager& instance_manager,
    const std::vector<std::string>& subcmd_args, const cvd_common::Envs& envs,
    const CommandRequest& request) {
  GroupCreationInfo creation_info = CF_EXPECT(AnalyzeCreation({
      .envs = envs,
      .selectors = request.Selectors(),
      .num_instances = num_instances_parser_.NumInstances(),
      .instance_ids = num_instances_parser_.InstanceIds(),
  }));

  auto groups = CF_EXPECT(instance_manager.FindGroups(
      {.group_name = creation_info.group_creation_params.group_name}));
  CF_EXPECTF(groups.empty(), "Group named '{}' already exists",
             creation_info.group_creation_params.group_name);
  return instance_manager.CreateInstanceGroup(
      std::move(creation_info.group_creation_params),
      std::move(creation_info.group_directories));
}


std::string CvdCreateCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdCreateCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  std::stringstream ss;

  ss << "cvd create - " << SummaryHelp() << "\n";
  ss << "\n";

  ss << FormatHelpText({
      HelpParagraph("Usage:"),

      HelpParagraph::Raw("  cvd create --config_file=PATH [LOAD_ARGS]"),
      HelpParagraph::Raw("  cvd create [--product_path=PATH] "
                         "[--host_path=PATH] [--[no]start] [START_ARGS]"),

      HelpParagraph(
          "The `cvd create` command operates in one of two distinct modes "
          "depending on whether the `--config_file` flag is provided."),

      HelpParagraph("ENVIRONMENT SPECIFICATION FILE MODE"),

      HelpParagraph("This mode executes when `--config_file` is provided"),
  });
  ss << FormatHelpText(LoadConfigsCommand::CommonCommandDescription());

  ss << FormatFlagsHelp(ConfigFileModeFlags());
  ss << FormatFlagsHelp(
      (CF_EXPECT(LoadConfigsCommand(instance_manager_).Flags(request))));

  ss << FormatHelpText({
      HelpParagraph("FLAG-BASED MODE"),

      HelpParagraph("When `--config_file` is NOT specified, `cvd create` "
                    "creates a local instance group using the host tools and "
                    "guest images provided via flags."),

      HelpParagraph(
          "By default, the instances in the group are started immediately. "
          "This behavior can be controlled with the `--start` flag; passing "
          "`--nostart` will create the group in a stopped state, allowing it "
          "to be started later with `cvd start`."),

      HelpParagraph(
          "The `--host_path` and `--product_path` flags specify where to find "
          "the Cuttlefish host tools and guest images, respectively. Their "
          "default values are designed to make `cvd create` 'just work' in "
          "common development environments. They default to the values of "
          "`$ANDROID_HOST_OUT` and `$ANDROID_PRODUCT_OUT` (typically set after "
          "running `lunch` in an Android build environment), falling back to "
          "`$HOME` or the current directory. This allows launching a locally "
          "built Cuttlefish target or a target downloaded to the current "
          "directory without additional configuration."),
  });

  ss << FormatFlagsHelp(BuildSelectorFlagsForCreateHelp(request.Selectors()));
  ss << FormatFlagsHelp(FlagModeFlags(request.Env(), request.Selectors()));

  ss << FormatHelpText({HelpParagraph(
      "The following flags control how the instances are started (unless "
      "--nostart is provided):")});

  ss << FormatFlagsHelp(
      (CF_EXPECT(CvdStartCommandHandler(instance_manager_).Flags(request))));

  return ss.str();
}

std::vector<Flag> CvdCreateCommandHandler::ConfigFileModeFlags() {
  own_flags_.config_file = "";
  return {GflagsCompatFlag("config_file", own_flags_.config_file)
              .Help("Path to an environment config file to be loaded.")};
}

std::vector<Flag> CvdCreateCommandHandler::FlagModeFlags(
    const cvd_common::Envs& env, const selector::SelectorOptions& selector_options) {
  own_flags_.host_path = DefaultHostPath(env);
  own_flags_.product_path = DefaultProductPath(env);
  own_flags_.start = true;
  std::vector<Flag> flags = num_instances_parser_.Flags(selector_options);
  flags.emplace_back(
      GflagsCompatFlag("host_path", own_flags_.host_path)
          .Help("The path to the directory containing the Cuttlefish Host "
                "Artifacts. Defaults to the value of $ANDROID_HOST_OUT, "
                "$HOME or the current directory."));
  flags.emplace_back(
      GflagsCompatFlag("product_path", own_flags_.product_path)
          .Help("The path(s) to the directory containing the Cuttlefish "
                "Guest Images. Defaults to the value of "
                "$ANDROID_PRODUCT_OUT, $HOME or the current directory."));
  flags.emplace_back(GflagsCompatFlag("start", own_flags_.start)
                         .Help("Whether to start the instance group."));
  return flags;
}

std::unique_ptr<CvdCommandHandler> NewCvdCreateCommandHandler(
    InstanceManager& instance_manager) {
  return std::make_unique<CvdCreateCommandHandler>(instance_manager);
}

}  // namespace cuttlefish
