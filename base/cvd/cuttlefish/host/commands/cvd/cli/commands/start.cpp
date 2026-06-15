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

#include <fcntl.h>
#include <signal.h>  // IWYU pragma: keep
#include <stddef.h>
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
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/fetch/substitute.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database_types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/operator_client.h"
#include "cuttlefish/host/commands/cvd/instances/stop.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/commands/cvd/utils/interrupt_listener.h"
#include "cuttlefish/host/commands/cvd/utils/subprocess_waiter.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/metrics/device_metrics_orchestration.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

std::optional<std::string> GetConfigPath(cvd_common::Args& args) {
  size_t initial_size = args.size();
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
  return absl::StrSplit(flag_value, ',');
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
                      group.Instances()[i].Name(), group.Instances()[i].Id());
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
  args.push_back("--webrtc_device_id=" + absl::StrJoin(webrtc_ids, ","));

  for (size_t i = 0; i < webrtc_ids.size(); ++i) {
    group.Instances()[i].SetWebRtcDeviceId(std::move(webrtc_ids[i]));
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
    ids.push_back(instance.Id());
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
    std::string flag_value = absl::StrJoin(ids, ",");
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
  CF_EXPECT(Symlink(config_from_home, link));
  return {};
}

Result<std::unique_ptr<OperatorControlConn>> PreregisterGroup(
    const LocalInstanceGroup& group) {
  std::unique_ptr<OperatorControlConn> operator_conn =
      CF_EXPECT(OperatorControlConn::Create());
  CF_EXPECT(operator_conn->Preregister(group));
  return operator_conn;
}

Result<void> CvdResetGroup(const LocalInstanceGroup& group) {
  // We can't run stop_cvd here. It may hang forever, and doesn't make sense
  // to interrupt it.
  const auto& instances = group.Instances();
  CF_EXPECT(!instances.empty());
  const auto& first_instance = *instances.begin();
  CF_EXPECT(ForcefullyStopGroup(first_instance.Id()));
  return {};
}

Result<void> UpdateEnvs(cvd_common::Envs& envs,
                        const LocalInstanceGroup& group) {
  CF_EXPECT(!group.Instances().empty());
  envs[kCuttlefishInstanceEnvVarName] =
      std::to_string(group.Instances()[0].Id());

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

Result<std::string> FindStartBin(const std::string& android_host_out) {
  return CF_EXPECT(HostToolTarget(android_host_out).GetStartBinName());
}

Result<Command> ConstructCvdNonHelpCommand(const std::string& bin_file,
                                           const LocalInstanceGroup& group,
                                           const cvd_common::Args& args,
                                           const cvd_common::Envs& envs,
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

Result<std::vector<Flag>> GetCvdInternalStartFlags(
    cvd_common::Args args, const cvd_common::Envs& env) {
  std::vector<Flag> flags =
      CF_EXPECT(GetSiblingCommandFlags("cvd_internal_start", env, args));
  // Remove flags set by cvd and intented to be exposed to the user
  const std::vector<std::string> to_remove = {
      "daemon", "instance_nums", "num_instances", "base_instance_num"};
  std::erase_if(flags,
                [&to_remove](Flag f) { return Contains(to_remove, f.Name()); });
  return flags;
}

}  // namespace

CvdStartCommandHandler::CvdStartCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

static Result<void> ConsumeDaemonModeFlag(cvd_common::Args& args) {
  bool daemon = true;
  Flag flag =
      GflagsCompatFlag("daemon", daemon)
          .AddValidator([&daemon]() -> Result<void> {
            CF_EXPECT(!!daemon, "`cvd start` must always run in daemon mode.");
            return {};
          });
  CF_EXPECT(ConsumeFlags({flag}, args));
  return {};
}

static bool HasUnsafeFlagsForBypass(const std::vector<std::string>& args) {
  std::vector<std::string> args_copy = args;
  bool daemon = true;
  std::string report_anonymous = "";
  std::vector<Flag> safe_flags = {
      GflagsCompatFlag("daemon", daemon),
      GflagsCompatFlag("report_anonymous_usage_stats", report_anonymous),
  };
  auto res = ConsumeFlags(safe_flags, args_copy);
  if (!res.ok()) {
    return true;
  }
  return !args_copy.empty();
}

Result<void> CvdStartCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> subcmd_args = request.SubcommandArguments();
  CF_EXPECT(!GetConfigPath(subcmd_args).has_value(),
            "The 'start' command doesn't accept --config_file, did you mean "
            "'create'?");

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return CF_ERR(NoGroupMessage(request));
  }

  if (request.Selectors().instance_names &&
      request.Selectors().instance_names->size() == 1) {
    auto [instance, group] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request));

    if (instance.State() == cvd::INSTANCE_STATE_STOPPED &&
        group.StartTime() != TimeStamp{} &&
        !HasUnsafeFlagsForBypass(subcmd_args)) {
      CF_EXPECT(LaunchSingleInstance(instance, group, request));
      return {};
    } else {
      VLOG(1) << "Instance is not in stopped state. Proceeding with "
                 "normal group start.";
    }
  }

  CF_EXPECT(ConsumeDaemonModeFlag(subcmd_args));
  subcmd_args.push_back("--daemon=true");

  LocalInstanceGroup group =
      CF_EXPECT(selector::SelectGroup(instance_manager_, request),
                "Failed to select group to start, did you mean 'cvd create'?");

  CF_EXPECT(!group.HasActiveInstances(),
            "Selected instance group is already started, use `cvd create` to "
            "create a new one.");

  cvd_common::Envs envs = request.Env();

  CF_EXPECT(UpdateInstanceArgs(subcmd_args, group));
  CF_EXPECT(UpdateWebrtcDeviceIds(subcmd_args, group));
  CF_EXPECT(UpdateEnvs(envs, group));
  const auto bin = CF_EXPECT(FindStartBin(group.HostArtifactsPath()));

  CF_EXPECT(ConsumeFlags(BuildOwnFlags(), subcmd_args));

  CF_EXPECT(HostPackageSubstitution(group.HostArtifactsPath(),
                                    own_flags_.host_substitutions));

  Command command = CF_EXPECT(
      ConstructCvdNonHelpCommand(bin, group, subcmd_args, envs, request));

  // The instance database needs to be updated if an interrupt is received.
  auto handle_res = PushInterruptListener([this, &group](int signal) {
    LOG(WARNING) << strsignal(signal) << " signal received, cleanning up";
    auto interrupt_res = subprocess_waiter_.Interrupt();
    if (!interrupt_res.ok()) {
      LOG(ERROR) << "Failed to stop subprocesses: " << interrupt_res.error();
      LOG(ERROR) << "Devices may still be executing in the background, run "
                    "`cvd reset` to ensure a clean state";
    }

    group.SetAllStates(cvd::INSTANCE_STATE_CANCELLED);
    auto update_res = instance_manager_.UpdateInstanceGroup(group);
    if (!update_res.ok()) {
      LOG(ERROR) << "Failed to update group status: " << update_res.error();
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

  // show user a more succinct output
  if (isatty(0)) {
    std::vector<std::string> instance_names;
    for (const auto& instance : group.Instances()) {
      instance_names.push_back(instance.Name());
    }
    std::cout << fmt::format("group:{}|instance(s):{}\n", group.GroupName(),
                             absl::StrJoin(instance_names, ","));
  } else {
    auto group_json = CF_EXPECT(group.FetchStatus());
    std::cout << group_json.toStyledString();
  }

  return {};
}

cvd_common::Args CvdStartCommandHandler::CmdList() const {
  return {"start", "launch_cvd"};
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
               << conn_res.error();
  }
  VLOG(0) << "launch command: " << launch_command;

  CF_EXPECT(subprocess_waiter_.Setup(launch_command));

  GatherVmStartMetrics(group);

  // NOLINTNEXTLINE(misc-include-cleaner)
  siginfo_t infop = CF_EXPECT(subprocess_waiter_.Wait());
  // NOLINTNEXTLINE(misc-include-cleaner)
  if (infop.si_code != CLD_EXITED || infop.si_status != EXIT_SUCCESS) {
    LOG(INFO) << "Device launch failed, cleaning up";
    GatherVmBootFailedMetrics(group);
    // run_cvd processes may be still running in background
    // the order of the following operations should be kept
    CF_EXPECT(CvdResetGroup(group));
  }
  CF_EXPECT(CheckProcessExitedNormally(infop));
  GatherVmBootCompleteMetrics(group);
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
               << symlink_config_res.error();
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

Result<void> CvdStartCommandHandler::LaunchSingleInstance(
    LocalInstance& instance, LocalInstanceGroup& group,
    const CommandRequest& request) {
  auto bin_path = group.HostArtifactsPath() + "/bin/run_cvd";
  cvd_common::Envs run_cvd_envs = request.Env();
  run_cvd_envs[kCuttlefishInstanceEnvVarName] = std::to_string(instance.Id());
  run_cvd_envs["HOME"] = group.HomeDir();
  run_cvd_envs[kAndroidHostOut] = group.HostArtifactsPath();
  run_cvd_envs[kAndroidProductOut] = group.ProductOutPath();
  run_cvd_envs[kAndroidSoongHostOut] = group.HostArtifactsPath();
  run_cvd_envs[kCvdMarkEnv] = "true";

  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = group.HomeDir(),
                                            .args = cvd_common::Args{},
                                            .envs = run_cvd_envs,
                                            .working_dir = CurrentDirectory(),
                                            .command_name = "run_cvd"};

  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                        Subprocess::StdIOChannel::kStdErr);
  SharedFD dev_null = SharedFD::Open("/dev/null", O_RDONLY);
  if (dev_null->IsOpen()) {
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, dev_null);
  } else {
    LOG(ERROR) << "Failed to open /dev/null: " << dev_null->StrError();
  }

  auto symlink_config_res = SymlinkPreviousConfig(group.HomeDir());
  if (!symlink_config_res.ok()) {
    LOG(ERROR) << "Failed to symlink the config file at system wide home: "
               << symlink_config_res.error();
  }

  auto set_instance_state = [&group, &instance](cvd::InstanceState state) {
    for (auto& inst : group.Instances()) {
      if (inst.Id() == instance.Id()) {
        inst.SetState(state);
        break;
      }
    }
  };

  set_instance_state(cvd::INSTANCE_STATE_STARTING);
  group.SetStartTime(CvdServerClock::now());
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));

  Result<void> start_res =
      LaunchDevice(std::move(command), group, run_cvd_envs, request);

  if (!start_res.ok()) {
    set_instance_state(cvd::INSTANCE_STATE_BOOT_FAILED);
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
    return start_res;
  }

  set_instance_state(cvd::INSTANCE_STATE_RUNNING);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));

  auto group_json = CF_EXPECT(group.FetchStatus());
  std::cout << group_json.toStyledString();

  return {};
}

std::vector<HelpParagraph> CvdStartCommandHandler::Description() const {
  std::vector<HelpParagraph> description;
  description.emplace_back(
      "The `cvd start` command applies to the instance group, not specific "
      "instances because Cuttlefish instances in the same group must all be "
      "started (and stopped) in unisom. The group to be started is chosen "
      "using the standard selector flags.");
  description.emplace_back(
      "Flags that modify individual instances accept a comma separated list of "
      "values. If the number of values in one of these flags is less than the "
      "number of instances, then the last instances in the group will default "
      "to the first value in the list (as a result a single value provided "
      "applies to all instances). The empty string is interpreted as a valid "
      "value, to force a particular instance to use its intended default value "
      "the special value \"unset\" must be given.");
  return description;
}

Result<std::vector<Flag>> CvdStartCommandHandler::Flags(
    const CommandRequest& request) {
  std::vector<Flag> own_flags = BuildOwnFlags();

  std::vector<Flag> internal_flags = CF_EXPECT(GetCvdInternalStartFlags(
      request.SubcommandArguments(), cvd_common::Envs()));

  std::vector<Flag> flags = std::move(own_flags);
  flags.insert(flags.end(), internal_flags.begin(), internal_flags.end());

  return flags;
}

std::vector<Flag> CvdStartCommandHandler::BuildOwnFlags() {
  return {GflagsCompatFlag("host_substitutions", own_flags_.host_substitutions)
              .Help("Comma separated list of files to replace in the host "
                    "artifacts from the android build with artifacts from the "
                    "cuttlefish-base package. The special value \"all\" causes "
                    "it to replace everything it can, while with an empty it "
                    "will replace what the android build specifies in the "
                    "'debian_substitution_marker' file.")};
}

std::unique_ptr<CvdCommandHandler> NewCvdStartCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdStartCommandHandler(instance_manager));
}

}  // namespace cuttlefish
