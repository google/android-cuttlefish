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

#include "cuttlefish/host/commands/cvd/cli/commands/start.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fmt/core.h>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/fetch/substitute.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database_types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/host/commands/cvd/instances/operator_client.h"
#include "cuttlefish/host/commands/cvd/instances/reset_client_utils.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/commands/cvd/utils/interrupt_listener.h"
#include "cuttlefish/host/commands/cvd/utils/subprocess_waiter.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Start a Cuttlefish virtual device or environment";

constexpr char kDetailedHelpText[] =
    "Run cvd start --help for the full help text.";

std::optional<std::string> GetConfigPath(cvd_common::Args& args) {
  std::size_t initial_size = args.size();
  std::string config_file;
  std::vector<Flag> config_flags = {
      GflagsCompatFlag("config_file", config_file)};
  auto result = ConsumeFlags(config_flags, args);
  if (!result.ok() || initial_size == args.size()) {
    return std::nullopt;
  }
  return config_file;
}

/**
 * Runs simple tests to see if it could potentially be a host artifacts dir
 *
 */
bool PotentiallyHostArtifactsPath(const std::string& host_artifacts_path) {
  if (host_artifacts_path.empty() || !DirectoryExists(host_artifacts_path)) {
    return false;
  }
  const auto host_bin_path = host_artifacts_path + "/bin";
  auto contents_result = DirectoryContents(host_bin_path);
  if (!contents_result.ok()) {
    return false;
  }
  std::vector<std::string> contents = std::move(*contents_result);
  std::set<std::string> contents_set{std::move_iterator(contents.begin()),
                                     std::move_iterator(contents.end())};
  std::set<std::string> launchers = {"cvd_internal_start", "launch_cvd"};
  std::vector<std::string> result;
  std::set_intersection(launchers.cbegin(), launchers.cend(),
                        contents_set.cbegin(), contents_set.cend(),
                        std::back_inserter(result));
  return !result.empty();
}

Result<std::vector<std::string>> ExtractWebRTCDeviceIds(
    cvd_common::Args& args) {
  std::string flag_value;
  std::vector<Flag> webrtc_device_id_flag{
      GflagsCompatFlag("webrtc_device_id", flag_value)};
  CF_EXPECT(ConsumeFlags(webrtc_device_id_flag, args));

  if (flag_value.empty()) {
    return {};
  }
  return android::base::Split(flag_value, ",");
}

// Some webrtc device ids could be empty (for example, when not specified in the
// load proto). Replace the empty ones with generated ones.
Result<std::vector<std::string>> ReplaceEmptyWebRTCDeviceIds(
    const LocalInstanceGroup& group, std::vector<std::string> webrtc_ids) {
  // Ensure the number of ids matches the number of instances.
  while (webrtc_ids.size() < group.Instances().size()) {
    webrtc_ids.push_back("");
  }
  CF_EXPECT_EQ(webrtc_ids.size(), group.Instances().size(),
               "Specified more webrtc device ids than instances");
  std::set<std::string> used_ids;
  for (const auto& webrtc_id : webrtc_ids) {
    if (!webrtc_id.empty()) {
      used_ids.insert(webrtc_id);
    }
  }
  for (int i = 0; i < webrtc_ids.size(); ++i) {
    if (webrtc_ids[i].empty()) {
      std::string generated_id =
          fmt::format("{}-{}-{}", group.GroupName(),
                      group.Instances()[i].name(), group.Instances()[i].id());
      webrtc_ids[i] = generated_id;
      // In the unlikely case that a provided device id matches one of the
      // generated ones append _{n} to the generated one, with n starting at 1
      // and growing as much as necessary to avoid a collision.
      for (int j = 1; used_ids.find(webrtc_ids[i]) != used_ids.end(); ++j) {
        webrtc_ids[i] = fmt::format("{}_{}", generated_id, j);
      }
      used_ids.insert(webrtc_ids[i]);
    }
  }
  return webrtc_ids;
}

Result<void> UpdateWebrtcDeviceIds(cvd_common::Args& args,
                                   LocalInstanceGroup& group) {
  std::vector<std::string> webrtc_ids = CF_EXPECT(ReplaceEmptyWebRTCDeviceIds(
      group, CF_EXPECT(ExtractWebRTCDeviceIds(args))));
  args.push_back("--webrtc_device_id=" + android::base::Join(webrtc_ids, ","));

  for (size_t i = 0; i < webrtc_ids.size(); ++i) {
    group.Instances()[i].set_webrtc_device_id(std::move(webrtc_ids[i]));
  }
  return {};
}

/*
 * 1. Remove --num_instances, --instance_nums, --base_instance_num if any.
 * 2. If the ids are consecutive and ordered, add:
 *   --base_instance_num=min --num_instances=ids.size()
 * 3. If not, --instance_nums=<ids>
 *
 */
static Result<void> UpdateInstanceArgs(cvd_common::Args& args,
                                       const LocalInstanceGroup& group) {
  CF_EXPECT(!group.Instances().empty());

  std::string old_instance_nums;
  std::string old_num_instances;
  std::string old_base_instance_num;

  std::vector<Flag> instance_id_flags{
      GflagsCompatFlag("instance_nums", old_instance_nums),
      GflagsCompatFlag("num_instances", old_num_instances),
      GflagsCompatFlag("base_instance_num", old_base_instance_num)};
  // discard old ones
  CF_EXPECT(ConsumeFlags(instance_id_flags, args));

  std::vector<unsigned> ids;
  for (const auto& instance : group.Instances()) {
    ids.push_back(instance.id());
  }
  auto first_id = *ids.begin();
  bool have_consecutive_ids = true;
  for (size_t i = 1; i < ids.size(); ++i) {
    if (ids[i] != first_id + i) {
      have_consecutive_ids = false;
      break;
    }
  }

  if (!have_consecutive_ids) {
    std::string flag_value = android::base::Join(ids, ",");
    args.push_back("--instance_nums=" + flag_value);
    return {};
  }

  // sorted and consecutive, so let's use old flags
  // like --num_instances and --base_instance_num
  args.push_back("--num_instances=" + std::to_string(ids.size()));
  args.push_back("--base_instance_num=" + std::to_string(first_id));
  return {};
}

Result<void> SymlinkPreviousConfig(const std::string& group_home_dir) {
  auto system_wide_home = CF_EXPECT(SystemWideUserHome());
  auto config_from_home = system_wide_home + "/.cuttlefish_config.json";
  if (!FileExists(config_from_home) || !LoadFromFile(config_from_home).ok()) {
    // Skip if the file doesn't exist or can't be parsed as JSON
    return {};
  }
  auto link = group_home_dir + "/.cuttlefish_config.json";
  if (FileExists(link, /* follow_symlinks */ false)) {
    // No need to create a symlink after this device has been started at least
    // once
    return {};
  }
  CF_EXPECTF(symlink(config_from_home.c_str(), link.c_str()) == 0,
             "symlink(\"{}\", \"{}\") failed: {}", config_from_home, link,
             std::strerror(errno));
  return {};
}

Result<std::unique_ptr<OperatorControlConn>> PreregisterGroup(
    const LocalInstanceGroup& group) {
  std::unique_ptr<OperatorControlConn> operator_conn =
      CF_EXPECT(OperatorControlConn::Create());
  CF_EXPECT(operator_conn->Preregister(group));
  return operator_conn;
}

}  // namespace

class CvdStartCommandHandler : public CvdCommandHandler {
 public:
  CvdStartCommandHandler(InstanceManager& instance_manager)
      : instance_manager_(instance_manager) {}

  Result<void> Handle(const CommandRequest& request) override;
  std::vector<std::string> CmdList() const override {
    return {"start", "launch_cvd"};
  }
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  Result<void> LaunchDevice(Command command, LocalInstanceGroup& group,
                            const cvd_common::Envs& envs,
                            const CommandRequest& request);

  Result<void> LaunchDeviceInterruptible(Command command,
                                         LocalInstanceGroup& group,
                                         const cvd_common::Envs& envs,
                                         const CommandRequest& request);

  Result<Command> ConstructCvdNonHelpCommand(const std::string& bin_file,
                                             const LocalInstanceGroup& group,
                                             const cvd_common::Args& args,
                                             const cvd_common::Envs& envs,
                                             const CommandRequest& request);

  struct GroupAndLockFiles {
    LocalInstanceGroup group;
    std::vector<InstanceLockFile> lock_files;
  };

  Result<void> UpdateArgs(cvd_common::Args& args, LocalInstanceGroup& group);

  Result<void> UpdateEnvs(cvd_common::Envs& envs,
                          const LocalInstanceGroup& group);

  Result<std::string> FindStartBin(const std::string& android_host_out);

  Result<void> AcloudCompatActions(const LocalInstanceGroup& group,
                                   const cvd_common::Envs& envs,
                                   const CommandRequest& request);
  InstanceManager& instance_manager_;
  SubprocessWaiter subprocess_waiter_;
};

Result<void> CvdStartCommandHandler::AcloudCompatActions(
    const LocalInstanceGroup& group, const cvd_common::Envs& envs,
    const CommandRequest& request) {
  // rm -fr "InstanceLocksPath()/local-instance-<i>"
  std::string acloud_compat_home_prefix =
      InstanceLocksPath() + "/local-instance-";
  std::vector<std::string> acloud_compat_homes;
  acloud_compat_homes.reserve(group.Instances().size());
  for (const auto& instance : group.Instances()) {
    acloud_compat_homes.push_back(
        ConcatToString(acloud_compat_home_prefix, instance.id()));
  }
  for (const auto& acloud_compat_home : acloud_compat_homes) {
    bool result_deleted = true;
    std::stringstream acloud_compat_home_stream;
    if (!FileExists(acloud_compat_home)) {
      continue;
    }
    if (!Contains(envs, kLaunchedByAcloud) ||
        envs.at(kLaunchedByAcloud) != "true") {
      if (!DirectoryExists(acloud_compat_home, /*follow_symlinks=*/false)) {
        // cvd created a symbolic link
        result_deleted = RemoveFile(acloud_compat_home);
      } else {
        // acloud created a directory
        // rm -fr isn't supporetd by TreeHugger, so if we fork-and-exec to
        // literally run "rm -fr", the presubmit testing may fail if ever this
        // code is tested in the future.
        result_deleted = RecursivelyRemoveDirectory(acloud_compat_home).ok();
      }
    }
    if (!result_deleted) {
      LOG(ERROR) << "Removing " << acloud_compat_home << " failed.";
      continue;
    }
  }

  const std::string& home_dir = group.HomeDir();
  CF_EXPECT(EnsureDirectoryExists(home_dir, 0775, /* group_name */ ""),
            "Failed to create group's home directory");
  const std::string& android_host_out = group.HostArtifactsPath();
  CF_EXPECT(CreateSymLink(android_host_out, home_dir + "/host_bins",
                          /* override_existing*/ true),
            "Failed to symlink host artifacts path to group's HOME directory");
  /* TODO(weihsu@): cvd acloud delete/list must handle multi-tenancy gracefully
   *
   * acloud delete just calls, for all instances in a group,
   *  /tmp/acloud_cvd_temp/local-instance-<i>/host_bins/stop_cvd
   *
   * That isn't necessary. Not desirable. Cvd acloud should read the instance
   * manager's in-memory data structure, and call stop_cvd once for the entire
   * group.
   *
   * Likewise, acloud list simply shows all instances in a flattened way. The
   * user has no clue about an instance group. Cvd acloud should show the
   * hierarchy.
   *
   * For now, we create the symbolic links so that it is compatible with acloud
   * in Python.
   */
  for (const auto& acloud_compat_home : acloud_compat_homes) {
    if (acloud_compat_home == home_dir) {
      LOG(ERROR) << "The \"HOME\" directory is acloud workspace, which will "
                 << "be deleted by next cvd start or acloud command with the"
                 << " same directory being \"HOME\"";
      continue;
    }
    auto link_res = CreateSymLink(home_dir, acloud_compat_home,
                                  /* override_existing*/ true);
    if (!link_res.ok()) {
      LOG(ERROR) << "Failed to symlink group's HOME directory to acloud "
                    "compatible location";
    }
  }
  return {};
}

Result<void> CvdStartCommandHandler::UpdateArgs(cvd_common::Args& args,
                                                LocalInstanceGroup& group) {
  CF_EXPECT(UpdateInstanceArgs(args, group));
  CF_EXPECT(UpdateWebrtcDeviceIds(args, group));
  return {};
}

Result<void> CvdStartCommandHandler::UpdateEnvs(
    cvd_common::Envs& envs, const LocalInstanceGroup& group) {
  CF_EXPECT(!group.Instances().empty());
  envs[kCuttlefishInstanceEnvVarName] =
      std::to_string(group.Instances()[0].id());

  envs["HOME"] = group.HomeDir();
  envs[kAndroidHostOut] = group.HostArtifactsPath();
  envs[kAndroidProductOut] = group.ProductOutPath();
  /* b/253644566
   *
   * Old branches used kAndroidSoongHostOut instead of kAndroidHostOut
   */
  envs[kAndroidSoongHostOut] = group.HostArtifactsPath();
  envs[kCvdMarkEnv] = "true";
  return {};
}

Result<Command> CvdStartCommandHandler::ConstructCvdNonHelpCommand(
    const std::string& bin_file, const LocalInstanceGroup& group,
    const cvd_common::Args& args, const cvd_common::Envs& envs,
    const CommandRequest& request) {
  auto bin_path = group.HostArtifactsPath();
  CF_EXPECTF(PotentiallyHostArtifactsPath(bin_path),
             "ANDROID_HOST_OUT, \"{}\" is not a tool directory", bin_path);
  bin_path.append("/bin/").append(bin_file);
  CF_EXPECT(!group.HomeDir().empty());
  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = group.HomeDir(),
                                            .args = args,
                                            .envs = envs,
                                            .working_dir = CurrentDirectory(),
                                            .command_name = bin_file};
  Command non_help_command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  // Print everything to stderr, cvd needs to print JSON to stdout which
  // would be unparseable with the subcommand's output.
  non_help_command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                                 Subprocess::StdIOChannel::kStdErr);
  return non_help_command;
}

Result<std::string> CvdStartCommandHandler::FindStartBin(
    const std::string& android_host_out) {
  return CF_EXPECT(HostToolTarget(android_host_out).GetStartBinName());
}

static Result<void> ConsumeDaemonModeFlag(cvd_common::Args& args) {
  Flag flag =
      Flag()
          .Alias({FlagAliasMode::kFlagPrefix, "-daemon="})
          .Alias({FlagAliasMode::kFlagPrefix, "--daemon="})
          .Alias({FlagAliasMode::kFlagExact, "-daemon"})
          .Alias({FlagAliasMode::kFlagExact, "--daemon"})
          .Alias({FlagAliasMode::kFlagExact, "-nodaemon"})
          .Alias({FlagAliasMode::kFlagExact, "--nodaemon"})
          .Setter([](const FlagMatch& match) -> Result<void> {
            static constexpr char kPossibleCmds[] =
                "\"cvd start\" or \"launch_cvd\"";
            if (match.key == match.value) {
              CF_EXPECTF(match.key.find("no") == std::string::npos,
                         "--nodaemon is not supported by {}", kPossibleCmds);
              return {};
            }
            CF_EXPECTF(match.value.find(",") == std::string::npos,
                       "{} had a comma that is not allowed", match.value);
            static constexpr std::string_view kValidFalseStrings[] = {"n", "no",
                                                                      "false"};
            static constexpr std::string_view kValidTrueStrings[] = {"y", "yes",
                                                                     "true"};
            for (const auto& true_string : kValidTrueStrings) {
              if (android::base::EqualsIgnoreCase(true_string, match.value)) {
                return {};
              }
            }
            for (const auto& false_string : kValidFalseStrings) {
              CF_EXPECTF(
                  !android::base::EqualsIgnoreCase(false_string, match.value),
                  "\"{}{} was given and is not supported by {}", match.key,
                  match.value, kPossibleCmds);
            }
            return CF_ERRF(
                "Invalid --daemon option: {}{}. {} supports only "
                "\"--daemon=true\"",
                match.key, match.value, kPossibleCmds);
          });
  CF_EXPECT(ConsumeFlags({flag}, args));
  return {};
}

Result<void> CvdStartCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));

  std::string subcmd = request.Subcommand();
  std::vector<std::string> subcmd_args = request.SubcommandArguments();
  CF_EXPECT(!GetConfigPath(subcmd_args).has_value(),
            "The 'start' command doesn't accept --config_file, did you mean "
            "'create'?");

  cvd_common::Envs envs = request.Env();
  if (Contains(envs, "HOME")) {
    if (envs["HOME"].empty()) {
      envs.erase("HOME");
    } else {
      // As the end-user may override HOME, this could be a relative path
      // to client's pwd, or may include "~" which is the client's actual
      // home directory.
      auto client_pwd = CurrentDirectory();
      const auto given_home_dir = envs.at("HOME");
      /*
       * Imagine this scenario:
       *   client$ export HOME=/tmp/new/dir
       *   client$ HOME="~/subdir" cvd start
       *
       * The value of ~ isn't sent to the server. The server can't figure that
       * out as it might be overridden before the cvd start command.
       */
      CF_EXPECT(!android::base::StartsWith(given_home_dir, "~") &&
                    !android::base::StartsWith(given_home_dir, "~/"),
                "The HOME directory should not start with ~");
      envs["HOME"] = CF_EXPECT(
          EmulateAbsolutePath({.current_working_dir = client_pwd,
                               .home_dir = CF_EXPECT(SystemWideUserHome()),
                               .path_to_convert = given_home_dir,
                               .follow_symlink = false}));
    }
  }
  // update DB if not help
  // collect group creation infos
  const bool is_help = CF_EXPECT(HasHelpFlag(subcmd_args));

  if (is_help) {
    auto android_host_out =
        CF_EXPECT(AndroidHostPath(envs),
                  "\nTry running this command from the same directory as the "
                  "downloaded or fetched host tools.");
    const auto bin = CF_EXPECT(FindStartBin(android_host_out));

    Command command =
        CF_EXPECT(ConstructCvdHelpCommand(bin, envs, subcmd_args, request));
    LOG(INFO) << "help command: " << command;

    siginfo_t infop;  // NOLINT(misc-include-cleaner)
    command.Start().Wait(&infop, WEXITED);
    // gflags (and flag_parser for compatibility) exit with 1 after help output
    CF_EXPECT(CheckProcessExitedNormally(infop, 1));
    return {};
  }

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return CF_ERR(NoGroupMessage(request));
  }

  CF_EXPECT(ConsumeDaemonModeFlag(subcmd_args));
  subcmd_args.push_back("--daemon=true");

  auto group =
      CF_EXPECT(selector::SelectGroup(instance_manager_, request),
                "Failed to select group to start, did you mean 'cvd create'?");

  CF_EXPECT(!group.HasActiveInstances(),
            "Selected instance group is already started, use `cvd create` to "
            "create a new one.");

  CF_EXPECT(UpdateArgs(subcmd_args, group));
  CF_EXPECT(UpdateEnvs(envs, group));
  const auto bin = CF_EXPECT(FindStartBin(group.HostArtifactsPath()));

  std::vector<std::string> host_substitutions;
  Flag host_substitutions_flag =
      GflagsCompatFlag("host_substitutions", host_substitutions);

  CF_EXPECT(
      ConsumeFlags(std::vector<Flag>{host_substitutions_flag}, subcmd_args));

  CF_EXPECT(
      HostPackageSubstitution(group.HostArtifactsPath(), host_substitutions));

  Command command = CF_EXPECT(
      ConstructCvdNonHelpCommand(bin, group, subcmd_args, envs, request));

  // The instance database needs to be updated if an interrupt is received.
  auto handle_res = PushInterruptListener([this, &group](int signal) {
    LOG(WARNING) << strsignal(signal) << " signal received, cleanning up";
    auto interrupt_res = subprocess_waiter_.Interrupt();
    if (!interrupt_res.ok()) {
      LOG(ERROR) << "Failed to stop subprocesses: "
                 << interrupt_res.error().FormatForEnv();
      LOG(ERROR) << "Devices may still be executing in the background, run "
                    "`cvd reset` to ensure a clean state";
    }

    group.SetAllStates(cvd::INSTANCE_STATE_CANCELLED);
    auto update_res = instance_manager_.UpdateInstanceGroup(group);
    if (!update_res.ok()) {
      LOG(ERROR) << "Failed to update group status: "
                 << update_res.error().FormatForEnv();
    }
    // It's technically possible for the group's state to be set to
    // "running" before abort has a chance to run, but that can only happen
    // if the instances are indeed running, so it's OK.

    std::abort();
  });
  auto listener_handle = CF_EXPECT(std::move(handle_res));
  group.SetAllStates(cvd::INSTANCE_STATE_STARTING);
  group.SetStartTime(CvdServerClock::now());
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  CF_EXPECT(
      LaunchDeviceInterruptible(std::move(command), group, envs, request));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  listener_handle.reset();

  auto group_json = CF_EXPECT(group.FetchStatus());
  std::cout << group_json.toStyledString();

  return {};
}

static Result<void> CvdResetGroup(const LocalInstanceGroup& group) {
  // We can't run stop_cvd here. It may hang forever, and doesn't make sense
  // to interrupt it.
  const auto& instances = group.Instances();
  CF_EXPECT(!instances.empty());
  const auto& first_instance = *instances.begin();
  CF_EXPECT(ForcefullyStopGroup(first_instance.id()));
  return {};
}

Result<void> CvdStartCommandHandler::LaunchDevice(
    Command launch_command, LocalInstanceGroup& group,
    const cvd_common::Envs& envs, const CommandRequest& request) {
  // Don't destroy the returned object until after the devices have started, it
  // holds a connection to the orchestrator that ensures the devices remain
  // pre-registered there. If the connection is lost before the devices register
  // themselves the pre-registration is lost and group information won't be
  // shown in the UI.
  auto conn_res = PreregisterGroup(group);
  if (!conn_res.ok()) {
    LOG(ERROR) << "Failed to pre-register devices with operator, group "
                  "information won't show in the UI: "
               << conn_res.error().FormatForEnv();
  }
  LOG(INFO) << "launch command: " << launch_command;

  CF_EXPECT(subprocess_waiter_.Setup(launch_command));

  auto acloud_compat_action_result = AcloudCompatActions(group, envs, request);
  if (!acloud_compat_action_result.ok()) {
    LOG(ERROR) << acloud_compat_action_result.error().FormatForEnv();
    LOG(ERROR) << "AcloudCompatActions() failed"
               << " but continue as they are minor errors.";
  }

  siginfo_t infop = CF_EXPECT(subprocess_waiter_.Wait());
  // NOLINTNEXTLINE(misc-include-cleaner)
  if (infop.si_code != CLD_EXITED || infop.si_status != EXIT_SUCCESS) {
    LOG(INFO) << "Device launch failed, cleaning up";
    // run_cvd processes may be still running in background
    // the order of the following operations should be kept
    CF_EXPECT(CvdResetGroup(group));
  }
  CF_EXPECT(CheckProcessExitedNormally(infop));
  return {};
}

Result<void> CvdStartCommandHandler::LaunchDeviceInterruptible(
    Command command, LocalInstanceGroup& group, const cvd_common::Envs& envs,
    const CommandRequest& request) {
  // cvd_internal_start uses the config from the previous invocation to
  // determine the default value for the -report_anonymous_usage_stats flag so
  // we symlink that to the group's home directory, this link will be
  // overwritten later by cvd_internal_start itself.
  auto symlink_config_res = SymlinkPreviousConfig(group.HomeDir());
  if (!symlink_config_res.ok()) {
    LOG(ERROR) << "Failed to symlink the config file at system wide home: "
               << symlink_config_res.error().FormatForEnv();
  }
  Result<void> start_res =
      LaunchDevice(std::move(command), group, envs, request);
  if (!start_res.ok()) {
    group.SetAllStates(cvd::INSTANCE_STATE_BOOT_FAILED);
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
    return start_res;
  }

  return {};
}

Result<std::string> CvdStartCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

// TODO(b/315027339): Swap to true.  Will likely need to add `cvd::Request` as a
// parameter of DetailedHelp to match current implementation
bool CvdStartCommandHandler::ShouldInterceptHelp() const { return false; }

Result<std::string> CvdStartCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  return kDetailedHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdStartCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdStartCommandHandler(instance_manager));
}

}  // namespace cuttlefish
