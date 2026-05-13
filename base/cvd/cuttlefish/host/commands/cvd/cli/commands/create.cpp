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
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/cli/commands/load_configs.h"
#include "cuttlefish/host/commands/cvd/cli/commands/start.h"
#include "cuttlefish/host/commands/cvd/cli/selector/creation_analyzer.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
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

}  // namespace

class CvdCreateCommandHandler : public CvdCommandHandler {
 public:
  CvdCreateCommandHandler(InstanceManager& instance_manager)
      : instance_manager_(instance_manager) {}

  Result<void> Handle(const CommandRequest& request) override;
  std::vector<std::string> CmdList() const override { return {"create"}; }
  std::string SummaryHelp() const override;

  Result<std::string> DetailedHelp(const CommandRequest& request) const override;

 private:
  Result<LocalInstanceGroup> CreateGroup(const cvd_common::Args& subcmd_args,
                                         const cvd_common::Envs& envs,
                                         const CommandRequest& request);
  Result<void> CreateSymlinks(const LocalInstanceGroup& group);

  InstanceManager& instance_manager_;
};

Result<LocalInstanceGroup> CvdCreateCommandHandler::CreateGroup(
    const std::vector<std::string>& subcmd_args, const cvd_common::Envs& envs,
    const CommandRequest& request) {
  GroupCreationInfo creation_info = CF_EXPECT(AnalyzeCreation({
      .cmd_args = subcmd_args,
      .envs = envs,
      .selectors = request.Selectors(),
  }));

  auto groups = CF_EXPECT(instance_manager_.FindGroups(
      {.group_name = creation_info.group_creation_params.group_name}));
  CF_EXPECTF(groups.empty(), "Group named '{}' already exists",
             creation_info.group_creation_params.group_name);
  return instance_manager_.CreateInstanceGroup(
      std::move(creation_info.group_creation_params),
      std::move(creation_info.group_directories));
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
    std::string instance_home_dir = fmt::format(
        "{}/cuttlefish/instances/cvd-{}", group.HomeDir(), instance.id());
    if(!FileExists(instance_home_dir)) {
      // Legacy launchers create cuttlefish_runtime.{$ID}
      instance_home_dir = fmt::format(
          "{}/cuttlefish_runtime.{}", group.HomeDir(), instance.id());
    }
    CF_EXPECT(EnsureSymlink(instance_home_dir,
                            fmt::format("{}/cuttlefish_runtime.{}",
                                        system_wide_home, instance.id())));
    std::string cuttlefish_home_dir = group.HomeDir() + "/cuttlefish";
    if(FileExists(cuttlefish_home_dir)) {
      // Legacy Cuttlefish launchers don't create this directory so no need to
      // create a corresponding symlink in that case.
      CF_EXPECT(EnsureSymlink(cuttlefish_home_dir,
                              system_wide_home + "/cuttlefish"));
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
  std::vector<std::string> subcmd_args = request.SubcommandArguments();

  cvd_common::Envs envs = CF_EXPECT(GetEnvs(request));
  CreateFlags flags = CF_EXPECT(ParseCommandFlags(envs, subcmd_args));

  if (!flags.config_file.empty()) {
    CommandRequest subrequest =
        CF_EXPECT(CreateLoadCommand(request, subcmd_args, flags.config_file));
    std::unique_ptr<CvdCommandHandler> load_handler =
        NewLoadConfigsCommand(instance_manager_);
    CF_EXPECT(load_handler->Handle(subrequest));
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
  auto group = CF_EXPECT(CreateGroup(subcmd_args, envs, request));

  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));

  if (flags.start) {
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

std::string CvdCreateCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}



Result<std::string> CvdCreateCommandHandler::DetailedHelp(
    const CommandRequest& request) const {
  return kDetailedHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdCreateCommandHandler(
    InstanceManager& instance_manager) {
  return std::make_unique<CvdCreateCommandHandler>(instance_manager);
}

}  // namespace cuttlefish

