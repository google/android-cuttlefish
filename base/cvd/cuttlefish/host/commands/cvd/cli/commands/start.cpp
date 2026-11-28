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
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "fmt/core.h"
#include "fmt/format.h"
#include "gflags/gflags.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#ifdef __linux__
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor.h"
#endif
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/fetch/substitute.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database_types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/operator_client.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/commands/cvd/utils/interrupt_listener.h"
#include "cuttlefish/host/commands/cvd/utils/subprocess_waiter.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/metrics/device_metrics_orchestration.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

std::optional<std::string> GetConfigPath(std::vector<std::string>& args) {
  size_t initial_size = args.size();
  std::string config_file;
  std::vector<Flag> config_flags = {
      GflagsCompatFlag("config_file", config_file)};
  auto result = ConsumeFlags(config_flags, args);
  if (!result.has_value() || initial_size == args.size()) {
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
  if (!contents_result.has_value()) {
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
    std::vector<std::string>& args) {
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

Result<void> UpdateWebrtcDeviceIds(std::vector<std::string>& args,
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
static Result<void> UpdateInstanceArgs(std::vector<std::string>& args,
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
  if (!FileExists(config_from_home) ||
      !LoadFromFile(config_from_home).has_value()) {
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

Result<std::unique_ptr<OperatorControlConn>> PreregisterInstances(
    const LocalInstanceGroup& group,
    const std::vector<LocalInstance>& instances) {
  std::unique_ptr<OperatorControlConn> operator_conn =
      CF_EXPECT(OperatorControlConn::Create());
  CF_EXPECT(operator_conn->Preregister(group, instances));
  return operator_conn;
}

Result<void> CvdResetGroup(const LocalInstanceGroup& group,
                           InstanceManager& instance_manager) {
  // We can't run stop_cvd here. It may hang forever, and doesn't make sense
  // to interrupt it.
  const auto& instances = group.Instances();
  CF_EXPECT(!instances.empty());
  const auto& first_instance = *instances.begin();
  CF_EXPECT(instance_manager.ForcefullyStopGroup(first_instance.Id()));
  return {};
}

Result<void> UpdateEnvs(std::unordered_map<std::string, std::string>& envs,
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

Result<Command> ConstructCvdNonHelpCommand(
    const std::string& bin_file, const LocalInstanceGroup& group,
    const std::vector<std::string>& args,
    const std::unordered_map<std::string, std::string>& envs,
    const CommandRequest& request, SharedFD output_fd = SharedFD()) {
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
  if (output_fd->IsOpen()) {
    non_help_command.RedirectStdIO(Command::StdIoChannel::kStdOut, output_fd);
    non_help_command.RedirectStdIO(Command::StdIoChannel::kStdErr, output_fd);
  } else {
    // Print everything to stderr, cvd needs to print JSON to stdout which
    // would be unparseable with the subcommand's output.
    non_help_command.RedirectStdIO(Command::StdIoChannel::kStdOut,
                                   Command::StdIoChannel::kStdErr);
  }
  return non_help_command;
}

Result<std::vector<Flag>> GetCvdInternalStartFlags(
    std::vector<std::string> args,
    const std::unordered_map<std::string, std::string>& env) {
  std::vector<Flag> flags =
      CF_EXPECT(GetSiblingCommandFlags("cvd_internal_start", env, args));
  // Remove flags set by cvd and intented to be exposed to the user
  const std::vector<std::string> to_remove = {
      "daemon", "instance_nums", "num_instances", "base_instance_num",
      "report_anonymous_usage_stats"};
  std::erase_if(flags,
                [&to_remove](Flag f) { return Contains(to_remove, f.Name()); });
  return flags;
}

bool CanBypassToSingleInstance(const LocalInstance& instance,
                               const LocalInstanceGroup& group,
                               const std::vector<std::string>& subcmd_args) {
  if (instance.State() != cvd::INSTANCE_STATE_STOPPED &&
      instance.State() != cvd::INSTANCE_STATE_BOOT_FAILED) {
    return false;
  }
  if (group.StartTime() == TimeStamp{}) {
    return false;
  }

  std::vector<std::string> args_copy = subcmd_args;
  bool daemon = true;
  std::string report_anonymous_usage_stats;
  std::vector<Flag> safe_flags = {
      GflagsCompatFlag("daemon", daemon),
      GflagsCompatFlag("report_anonymous_usage_stats",
                       report_anonymous_usage_stats),
  };
  const Result<void> res = ConsumeFlags(safe_flags, args_copy);
  if (!res.has_value() || !daemon || !args_copy.empty()) {
    return false;
  }

  const std::vector<LocalInstance>& instances = group.Instances();
  if (instances.empty()) {
    return false;
  }
  const LocalInstance& main_instance = instances[0];
  if (instance.Id() == main_instance.Id()) {
    return false;
  }
  if (main_instance.State() != cvd::INSTANCE_STATE_RUNNING) {
    return false;
  }

  return true;
}

void ParseFlagsForMetrics(const CommandRequest& request) {
  std::vector<std::string> args = {"cvd", request.Subcommand()};
  args.insert(args.end(), request.SubcommandArguments().begin(),
              request.SubcommandArguments().end());

  std::vector<char*> pseudo_argv;
  for (std::string& arg : args) {
    pseudo_argv.push_back(arg.data());
  }
  int argc = pseudo_argv.size();
  auto argv = pseudo_argv.data();

  gflags::AllowCommandLineReparsing();  // Support future non-gflags flags
  gflags::ParseCommandLineNonHelpFlags(&argc, &argv,
                                       /* remove_flags */ false);
}

struct LaunchInterruptController {
  enum class Phase {
    kStarting,
    kMonitoring,
    kStopping,
    // Launch finished; suppresses late signals during listener teardown.
    kDone,
  };

  void StopMonitor() const {
    if (stop_write->IsOpen()) {
      (void)stop_write->Write("", 1);  // Write NUL.
    }
  }

  void OnSignal(int signal) {
    switch (phase.load()) {
      case Phase::kDone:
        return;
      case Phase::kMonitoring:
      case Phase::kStopping:
        HandlePostBootSignal();
        return;
      case Phase::kStarting:
        break;
    }
    LOG(WARNING) << strsignal(signal) << " signal received, cleaning up";
    if (Result<void> res = subprocess_waiter.Interrupt(); !res.has_value()) {
      LOG(ERROR) << "Failed to stop subprocesses: " << res.error();
      LOG(ERROR) << "Devices may still be executing in the background, "
                    "run `cvd reset` to ensure a clean state";
    }
    {
      std::lock_guard lock(group_mutex);
      if (phase.load() == Phase::kStarting) {
        StopMonitor();
        group.SetAllStates(cvd::INSTANCE_STATE_CANCELLED);
        if (Result<void> res = instance_manager.UpdateInstanceGroup(group);
            !res.has_value()) {
          LOG(ERROR) << "Failed to update group status: " << res.error();
        }
        std::abort();
      }
    }
    if (phase.load() != Phase::kDone) {
      HandlePostBootSignal();
    }
  }

  void HandlePostBootSignal() {
    if (phase.exchange(Phase::kStopping) != Phase::kStopping) {
      StopMonitor();
    } else {
      LOG(INFO) << "Already stopping device, please wait...";
    }
  }

  SubprocessWaiter& subprocess_waiter;
  InstanceManager& instance_manager;
  LocalInstanceGroup& group;
  std::mutex& group_mutex;
  SharedFD stop_write;
  std::atomic<Phase> phase = Phase::kStarting;
};

}  // namespace

CvdStartCommandHandler::CvdStartCommandHandler(
    InstanceManager& instance_manager,
    std::optional<Fd> preacquired_in_progress_lock)
    : instance_manager_(instance_manager),
      preacquired_in_progress_lock_(std::move(preacquired_in_progress_lock)) {
  own_flags_.daemon = true;
  own_flags_.print_group_format = isatty(0) ? "human" : "json";
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
    std::optional<Fd> local_lock;
    if (!preacquired_in_progress_lock_.has_value()) {
      local_lock =
          CF_EXPECT(InstanceManager::AcquireGroupInProgressLock(group));
    }
    group = CF_EXPECT(instance_manager_.RefreshInstanceGroupState(
        group, /*caller_holds_in_progress_lock=*/true));
    bool found_instance = false;
    for (const LocalInstance& refreshed_inst : group.Instances()) {
      if (refreshed_inst.Id() == instance.Id()) {
        instance = refreshed_inst;
        found_instance = true;
        break;
      }
    }
    CF_EXPECT(found_instance == true,
              "Selected instance no longer exists in group");

    if (CanBypassToSingleInstance(instance, group, subcmd_args)) {
      std::optional<Fd> lock = preacquired_in_progress_lock_.has_value()
                                   ? std::move(preacquired_in_progress_lock_)
                                   : std::move(local_lock);
      preacquired_in_progress_lock_.reset();
      CF_EXPECT(
          LaunchSingleInstance(instance, group, request, std::move(lock)));
      return {};
    } else {
      VLOG(1) << "Cannot bypass to single instance start. Proceeding with "
                 "normal group start.";
    }
  }

  CF_EXPECT(ConsumeFlags(BuildOwnFlags(), subcmd_args));
  subcmd_args.push_back("--daemon=true");

  LocalInstanceGroup group =
      CF_EXPECT(selector::SelectGroup(instance_manager_, request),
                "Failed to select group to start, did you mean 'cvd create'?");

  std::optional<Fd> in_progress_lock = std::move(preacquired_in_progress_lock_);
  preacquired_in_progress_lock_.reset();
  if (!in_progress_lock.has_value()) {
    in_progress_lock =
        CF_EXPECT(InstanceManager::AcquireGroupInProgressLock(group));
  }
  group = CF_EXPECT(instance_manager_.RefreshInstanceGroupState(
      group, /*caller_holds_in_progress_lock=*/true));

  CF_EXPECT(!group.HasActiveInstances(),
            "Selected instance group is already started, use `cvd create` to "
            "create a new one.");
  CF_EXPECT(instance_manager_.EnsureInstanceLocksInUse(group));

  std::unordered_map<std::string, std::string> envs = request.Env();

  CF_EXPECT(UpdateInstanceArgs(subcmd_args, group));
  CF_EXPECT(UpdateWebrtcDeviceIds(subcmd_args, group));
  CF_EXPECT(UpdateEnvs(envs, group));
  const auto bin = CF_EXPECT(FindStartBin(group.HostArtifactsPath()));

  CF_EXPECT(HostPackageSubstitution(group.HostArtifactsPath(),
                                    own_flags_.host_substitutions));

  SharedFD memfd;
  SharedFD stop_read;
  SharedFD stop_write;
  if (!own_flags_.daemon) {
    memfd = CF_EXPECT(Fd::MemfdCreate("cvd_internal_start_output"));
    CF_EXPECT(SharedFD::Pipe(&stop_read, &stop_write), stop_read->StrError());
  }

  Command command = CF_EXPECT(ConstructCvdNonHelpCommand(
      bin, group, subcmd_args, envs, request, memfd));

  std::mutex group_mutex;
  LaunchInterruptController interrupt_controller{
      .subprocess_waiter = subprocess_waiter_,
      .instance_manager = instance_manager_,
      .group = group,
      .group_mutex = group_mutex,
      .stop_write = stop_write,
  };
  std::unique_ptr<InterruptListenerHandle> listener_handle =
      CF_EXPECT(PushInterruptListener([&interrupt_controller](int signal) {
        interrupt_controller.OnSignal(signal);
      }));
  {
    std::lock_guard lock(group_mutex);
    group.SetAllStates(cvd::INSTANCE_STATE_STARTING);
    group.SetStartTime(CvdServerClock::now());
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  }

  Result<void> monitor_res;
#ifdef __linux__
  std::thread monitor_thread;

  if (!own_flags_.daemon) {
    const LocalInstance first_instance = *group.Instances().begin();
    const std::string assemble_log = absl::StrCat(
        first_instance.AssemblyDirectory(), "/", kLogNameAssembleCvd);
    (void)RemoveFile(assemble_log);
    for (const auto& log : CF_EXPECT(first_instance.LogsFilenames())) {
      (void)RemoveFile(log);
    }
    monitor_thread = std::thread([first_instance, stop_read, &monitor_res]() {
      monitor_res = MonitorLogs(first_instance, stop_read);
    });
  }
  auto wait_for_monitor = [&monitor_thread]() {
    if (monitor_thread.joinable()) {
      monitor_thread.join();
    }
  };
#else
  // Without the log monitor the foreground start just waits for the stop.
  auto wait_for_monitor = [stop_read]() {
    if (stop_read->IsOpen()) {
      char unused;
      (void)stop_read->Read(&unused, 1);
    }
  };
#endif

  Result<void> launch_res = LaunchDeviceInterruptible(
      std::move(command), group, group_mutex, envs, request);

  if (!launch_res.has_value()) {
    {
      std::lock_guard lock(group_mutex);
      interrupt_controller.phase.store(LaunchInterruptController::Phase::kDone);
    }
    listener_handle.reset();
    if (!own_flags_.daemon) {
      interrupt_controller.StopMonitor();
      wait_for_monitor();
      memfd->LSeek(0, SEEK_SET);
      std::cout << ReadToString(*memfd).value_or("") << std::flush;
    }
    return launch_res;
  }

  {
    std::lock_guard lock(group_mutex);
    interrupt_controller.phase.store(
        own_flags_.daemon ? LaunchInterruptController::Phase::kDone
                          : LaunchInterruptController::Phase::kMonitoring);
    group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  }
  in_progress_lock.reset();

  if (own_flags_.daemon) {
    listener_handle.reset();
  }
  GatherVmBootCompleteMetrics(group);

  if (!own_flags_.daemon) {
    wait_for_monitor();
    interrupt_controller.phase.store(
        LaunchInterruptController::Phase::kStopping);

    LOG(INFO) << "Stopping device...";
    CF_EXPECT(instance_manager_.StopInstanceGroup(
        group, InstanceDirActionOnStop::Keep));
    LOG(INFO) << "Device stopped.";
    return monitor_res;
  }

  if (own_flags_.print_group_format == "json") {
    auto group_json = CF_EXPECT(group.FetchStatus());
    std::cout << group_json.toStyledString();
  } else if (own_flags_.print_group_format == "human") {
    std::vector<std::string> instance_names;
    for (const auto& instance : group.Instances()) {
      instance_names.push_back(instance.Name());
    }
    std::cout << fmt::format("group:{}|instance(s):{}\n", group.GroupName(),
                             absl::StrJoin(instance_names, ","));
  }

  return {};
}

std::vector<std::string> CvdStartCommandHandler::CmdList() const {
  return {"start", "launch_cvd"};
}

Result<void> CvdStartCommandHandler::LaunchDevice(
    Command launch_command, LocalInstanceGroup& group,
    const std::vector<LocalInstance>& instances,
    const CommandRequest& request) {
  // Don't destroy the returned object until after the devices have started, it
  // holds a connection to the orchestrator that ensures the devices remain
  // pre-registered there. If the connection is lost before the devices register
  // themselves the pre-registration is lost and group information won't be
  // shown in the UI.
  const Result<std::unique_ptr<OperatorControlConn>> conn_res =
      PreregisterInstances(group, instances);
  if (!conn_res.has_value()) {
    LOG(ERROR) << "Failed to pre-register devices with operator, group "
                  "information won't show in the UI: "
               << conn_res.error();
  }
  VLOG(0) << "launch command: " << launch_command;

  CF_EXPECT(subprocess_waiter_.Setup(launch_command));

  ParseFlagsForMetrics(request);
  GatherVmStartMetrics(group);

  // NOLINTNEXTLINE(misc-include-cleaner)
  const siginfo_t infop = CF_EXPECT(subprocess_waiter_.Wait());
  // NOLINTNEXTLINE(misc-include-cleaner)
  if (infop.si_code != CLD_EXITED || infop.si_status != EXIT_SUCCESS) {
    LOG(INFO) << "Device launch failed, cleaning up";
    GatherVmBootFailedMetrics(group);
    if (instances.size() < group.Instances().size()) {
      std::vector<unsigned> ids;
      ids.reserve(instances.size());
      for (const LocalInstance& instance : instances) {
        ids.push_back(instance.Id());
      }
      const Result<void> stop_res = instance_manager_.StopInstanceGroup(
          group, InstanceDirActionOnStop::Keep, std::chrono::seconds(5), ids);
      if (!stop_res.has_value()) {
        LOG(ERROR) << "Failed to stop failed instance(s): " << stop_res.error();
      }
    } else {
      // run_cvd processes may be still running in background
      // the order of the following operations should be kept
      CF_EXPECT(CvdResetGroup(group, instance_manager_));
    }
  }
  CF_EXPECT(CheckProcessExitedNormally(infop));
  return {};
}

Result<void> CvdStartCommandHandler::LaunchDeviceInterruptible(
    Command command, LocalInstanceGroup& group, std::mutex& group_mutex,
    const std::unordered_map<std::string, std::string>& envs,
    const CommandRequest& request) {
  // cvd_internal_start uses the config from the previous invocation to
  // determine the default value for the -report_anonymous_usage_stats flag so
  // we symlink that to the group's home directory, this link will be
  // overwritten later by cvd_internal_start itself.
  // NOTE: --report_anonymous_usage_stats flag and its value in the config are
  // deprecated
  auto symlink_config_res = SymlinkPreviousConfig(group.HomeDir());
  if (!symlink_config_res.has_value()) {
    LOG(ERROR) << "Failed to symlink the config file at system wide home: "
               << symlink_config_res.error();
  }
  const Result<void> start_res =
      LaunchDevice(std::move(command), group, group.Instances(), request);
  if (!start_res.has_value()) {
    std::lock_guard lock(group_mutex);
    group.SetAllStates(cvd::INSTANCE_STATE_BOOT_FAILED);
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
    return start_res;
  }

  return {};
}

Result<void> CvdStartCommandHandler::LaunchSingleInstance(
    LocalInstance& instance, LocalInstanceGroup& group,
    const CommandRequest& request, std::optional<Fd> in_progress_lock) {
  CF_EXPECT(instance_manager_.EnsureInstanceLocksInUse(group));

  const std::string bin_path = group.HostArtifactsPath() + "/bin/run_cvd";
  std::unordered_map<std::string, std::string> run_cvd_envs = request.Env();
  run_cvd_envs[kCuttlefishInstanceEnvVarName] = std::to_string(instance.Id());
  run_cvd_envs["HOME"] = group.HomeDir();
  run_cvd_envs[kAndroidHostOut] = group.HostArtifactsPath();
  run_cvd_envs[kAndroidProductOut] = group.ProductOutPath();
  run_cvd_envs[kAndroidSoongHostOut] = group.HostArtifactsPath();
  run_cvd_envs[kCvdMarkEnv] = "true";

  const ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = group.HomeDir(),
      .args = std::vector<std::string>{},
      .envs = run_cvd_envs,
      .working_dir = instance.InstanceDirectory(),
      .command_name = "run_cvd"};

  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  command.RedirectStdIO(Command::StdIoChannel::kStdOut,
                        Command::StdIoChannel::kStdErr);
  Result<Fd> dev_null = Fd::Open("/dev/null", O_RDONLY);
  if (dev_null.has_value()) {
    command.RedirectStdIO(Command::StdIoChannel::kStdIn, std::move(*dev_null));
  } else {
    LOG(ERROR) << "Failed to open /dev/null: " << dev_null.error();
  }

  const Result<void> symlink_config_res =
      SymlinkPreviousConfig(group.HomeDir());
  if (!symlink_config_res.has_value()) {
    LOG(ERROR) << "Failed to symlink the config file at system wide home: "
               << symlink_config_res.error();
  }

  auto set_instance_state = [&group, &instance](cvd::InstanceState state) {
    for (LocalInstance& inst : group.Instances()) {
      if (inst.Id() == instance.Id()) {
        inst.SetState(state);
        break;
      }
    }
  };

  set_instance_state(cvd::INSTANCE_STATE_STARTING);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));

  const Result<void> start_res =
      LaunchDevice(std::move(command), group, {instance}, request);
  if (!start_res.has_value()) {
    set_instance_state(cvd::INSTANCE_STATE_BOOT_FAILED);
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
    return start_res;
  }

  set_instance_state(cvd::INSTANCE_STATE_RUNNING);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  in_progress_lock.reset();
  GatherVmBootCompleteMetrics(group);

  const Json::Value group_json = CF_EXPECT(group.FetchStatus());
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

  std::vector<Flag> internal_flags = CF_EXPECT(
      GetCvdInternalStartFlags(request.SubcommandArguments(),
                               std::unordered_map<std::string, std::string>()));

  std::vector<Flag> flags = std::move(own_flags);
  flags.insert(flags.end(), internal_flags.begin(), internal_flags.end());

  return flags;
}

std::vector<Flag> CvdStartCommandHandler::BuildOwnFlags() {
  return {
      GflagsCompatFlag("host_substitutions", own_flags_.host_substitutions)
          .Help("Comma separated list of files to replace in the host "
                "artifacts from the android build with artifacts from the "
                "cuttlefish-base package. The special value \"all\" causes "
                "it to replace everything it can, while with an empty it "
                "will replace what the android build specifies in the "
                "'debian_substitution_marker' file."),
      GflagsCompatFlag("daemon", own_flags_.daemon)
          .Help("Run the start command in the background as a daemon. "
                "If false, the command runs in the foreground and monitors "
                "logs."),
      GflagsCompatFlag("print_group_format", own_flags_.print_group_format)
          .Help("The format in which to print group information after start. "
                "Supported values are \"human\", \"json\", and \"none\"."),
  };
}

}  // namespace cuttlefish
