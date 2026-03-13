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
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/command_sequence.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/cli/selector/creation_analyzer.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database_types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/metrics/metrics_orchestration.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using selector::AnalyzeCreation;
using selector::CreationAnalyzerParam;
using selector::GroupCreationInfo;

constexpr char kSummaryHelpText[] =
    "Create a Cuttlefish virtual device or environment";

constexpr char kDetailedHelpText[] =
    R"""(
Usage:
cvd create [--product_path=PATH] [--host_path=PATH] [--[no]start] [START_ARGS]
cvd create --config_file=PATH [--[no]start]

Creates and starts a new cuttlefish instance group.

--host_path=PATH     The path to the directory containing the Cuttlefish Host
                     Artifacts. Defaults to the value of $ANDROID_HOST_OUT,
                     $HOME or the current directory.

--product_path=PATH  The path(s) to the directory containing the Cuttlefish
                     Guest Images. Defaults to the value of
                     $ANDROID_PRODUCT_OUT, $HOME or the current directory.

--[no]start          Whether to start the instance group. True by default.
--config_file=PATH   Path to an environment config file to be loaded.

--acquire_file_lock  If the flag is given, the cvd server attempts to acquire
                     the instance lock file lock. (default: true)

All other arguments are passed verbatim to cvd start, for a list of supported
arguments run `cvd start --help`.
)""";

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

struct CreateFlags {
  std::string host_path;
  std::string product_path;
  bool start;
  std::string config_file;
};

Result<CreateFlags> ParseCommandFlags(const cvd_common::Envs& envs,
                                      cvd_common::Args& args) {
  CreateFlags flag_values{
      .host_path = DefaultHostPath(envs),
      .product_path = DefaultProductPath(envs),
      .start = true,
      .config_file = "",
  };
  std::vector<Flag> flags = {
      GflagsCompatFlag("host_path", flag_values.host_path),
      GflagsCompatFlag("product_path", flag_values.product_path),
      GflagsCompatFlag("start", flag_values.start),
      GflagsCompatFlag("config_file", flag_values.config_file),
  };
  CF_EXPECT(ConsumeFlags(flags, args));
  return flag_values;
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

Result<CommandRequest> CreateStartCommand(const CommandRequest& request,
                                          const LocalInstanceGroup& group,
                                          const cvd_common::Args& args,
                                          const cvd_common::Envs& envs) {
  return CF_EXPECT(
      CommandRequestBuilder()
          .SetEnv(envs)
          .AddArguments({"cvd", "start"})
          .AddArguments(args)
          .AddSelectorArguments({"--group_name", group.GroupName()})
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
    auto client_pwd = CurrentDirectory();
    const auto given_home_dir = envs["HOME"];
    // Substituting ~ is not supported by cvd
    CF_EXPECT(!absl::StartsWith(given_home_dir, "~") &&
                  !absl::StartsWith(given_home_dir, "~/"),
              "The HOME directory should not start with ~");
    envs["HOME"] = CF_EXPECT(
        EmulateAbsolutePath({.current_working_dir = client_pwd,
                             .home_dir = CF_EXPECT(SystemWideUserHome()),
                             .path_to_convert = given_home_dir,
                             .follow_symlink = false}));
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

}  // namespace

class CvdCreateCommandHandler : public CvdCommandHandler {
 public:
  CvdCreateCommandHandler(InstanceManager& instance_manager,
                          CommandSequenceExecutor& command_executor)
      : instance_manager_(instance_manager),
        command_executor_(command_executor) {}

  Result<void> Handle(const CommandRequest& request) override;
  std::vector<std::string> CmdList() const override { return {"create"}; }
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  Result<LocalInstanceGroup> GetOrCreateGroup(
      const cvd_common::Args& subcmd_args, const cvd_common::Envs& envs,
      const CommandRequest& request);
  Result<void> CreateSymlinks(const LocalInstanceGroup& group);

  InstanceManager& instance_manager_;
  CommandSequenceExecutor& command_executor_;
};

Result<LocalInstanceGroup> CvdCreateCommandHandler::GetOrCreateGroup(
    const std::vector<std::string>& subcmd_args, const cvd_common::Envs& envs,
    const CommandRequest& request) {
  GroupCreationInfo creation_info = CF_EXPECT(AnalyzeCreation({
      .cmd_args = subcmd_args,
      .envs = envs,
      .selectors = request.Selectors(),
  }));

  auto groups = CF_EXPECT(instance_manager_.FindGroups(
      {.group_name = creation_info.group_creation_params.group_name}));
  CF_EXPECT_LE(groups.size(), 1u,
               "Expected no more than one group with given name: "
                   << creation_info.group_creation_params.group_name);
  // When loading an environment spec file the group is already in the database
  // in PREPARING state. Otherwise the group must be created.
  if (groups.empty()) {
    return instance_manager_.CreateInstanceGroup(
        std::move(creation_info.group_creation_params),
        std::move(creation_info.group_directories));
  }
  auto& group = groups[0];
  CF_EXPECTF((size_t)group.Instances().size() ==
                 creation_info.group_creation_params.instances.size(),
             "Mismatch in number of instances from analisys: {} vs {}",
             group.Instances().size(),
             creation_info.group_creation_params.instances.size());
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  return group;
}

// For backward compatibility, we add extra symlink in home dir
Result<void> CvdCreateCommandHandler::CreateSymlinks(
    const LocalInstanceGroup& group) {
  auto system_wide_home = CF_EXPECT(SystemWideUserHome());
  CF_EXPECT(EnsureDirectoryExists(group.HomeDir()));
  auto smallest_id = std::numeric_limits<unsigned>::max();
  for (const auto& instance : group.Instances()) {
    // later on, we link cuttlefish_runtime to cuttlefish_runtime.smallest_id
    smallest_id = std::min(smallest_id, instance.id());
    const std::string instance_home_dir = fmt::format(
        "{}/cuttlefish/instances/cvd-{}", group.HomeDir(), instance.id());
    CF_EXPECT(EnsureSymlink(instance_home_dir,
                            fmt::format("{}/cuttlefish_runtime.{}",
                                        system_wide_home, instance.id())));
    CF_EXPECT(EnsureSymlink(group.HomeDir() + "/cuttlefish",
                            system_wide_home + "/cuttlefish"));
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
      Copy(group.Instances()[0].instance_dir() + "/cuttlefish_config.json",
           CF_EXPECT(SystemWideUserHome()) + "/.cuttlefish_config.json"),
      "Failed to copy config file to home directory");

  const std::string instance_runtime_dir =
      fmt::format("{}/cuttlefish_runtime.{}", system_wide_home, smallest_id);
  const std::string runtime_dir_link = system_wide_home + "/cuttlefish_runtime";
  CF_EXPECT(EnsureSymlink(instance_runtime_dir, runtime_dir_link));
  return {};
}

Result<void> CvdCreateCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));
  std::vector<std::string> subcmd_args = request.SubcommandArguments();
  bool is_help = CF_EXPECT(HasHelpFlag(subcmd_args));
  CF_EXPECT(!is_help);

  cvd_common::Envs envs = CF_EXPECT(GetEnvs(request));
  CreateFlags flags = CF_EXPECT(ParseCommandFlags(envs, subcmd_args));

  if (!flags.config_file.empty()) {
    auto subrequest =
        CF_EXPECT(CreateLoadCommand(request, subcmd_args, flags.config_file));
    CF_EXPECT(command_executor_.ExecuteOne(subrequest, std::cerr));
    return {};
  }

  // Validate the host artifacts path before proceeding
  (void)CF_EXPECT(
      HostToolTarget(flags.host_path).GetStartBinName(),
      "\nCould not find the required host tools to launch a device.\n\n"
      "If you already have the host tools and devices images downloaded use "
      "the `--host_path` and `--product_path` flags.\nSee `cvd help create` "
      "for more details.\n\n"
      "If you need to download host tools or system images try using `cvd "
      "fetch`.\nFor example: `cvd fetch --default_build=<branch>/<target>`\n\n"
      "If you are building Android from source, try running `lunch <target>; "
      "m` to set up your environment and build the images.");
  // CreationAnalyzer needs these to be set in the environment
  envs[kAndroidHostOut] = AbsolutePath(flags.host_path);
  envs[kAndroidProductOut] = AbsolutePath(flags.product_path);
  auto group = CF_EXPECT(GetOrCreateGroup(subcmd_args, envs, request));

  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  group.SetStartTime(CvdServerClock::now());
  // TODO: b/471069557 - diagnose unused
  Result<void> unused = instance_manager_.UpdateInstanceGroup(group);

  GatherVmInstantiationMetrics(group);

  if (flags.start) {
    auto start_cmd =
        CF_EXPECT(CreateStartCommand(request, group, subcmd_args, envs));
    CF_EXPECT(command_executor_.ExecuteOne(start_cmd, std::cerr));
    // For backward compatibility, we add extra symlink in system wide home
    // when HOME is NOT overridden and selector flags are NOT given.
    auto is_default_group =
        StringFromEnv("HOME", "") == CF_EXPECT(SystemWideUserHome()) &&
        !request.Selectors().HasOptions();

    if (is_default_group) {
      auto symlink_res = CreateSymlinks(group);
      if (!symlink_res.ok()) {
        LOG(ERROR) << "Failed to create symlinks for default group: "
                   << symlink_res.error();
      }
    }
  }

  return {};
}

Result<std::string> CvdCreateCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdCreateCommandHandler::ShouldInterceptHelp() const { return true; }

Result<std::string> CvdCreateCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  return kDetailedHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdCreateCommandHandler(
    InstanceManager& instance_manager, CommandSequenceExecutor& executor) {
  return std::make_unique<CvdCreateCommandHandler>(instance_manager, executor);
}

}  // namespace cuttlefish

