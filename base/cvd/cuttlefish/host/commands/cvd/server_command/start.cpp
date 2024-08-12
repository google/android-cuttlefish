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

#include "host/commands/cvd/server_command/start.h"

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/group_selector.h"
#include "host/commands/cvd/interrupt_listener.h"
#include "host/commands/cvd/reset_client_utils.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/status_fetcher.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/config_constants.h"

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

// link might be a directory, so we clean that up, and create a link from
// target to link
Result<void> EnsureSymlink(const std::string& target, const std::string link) {
  if (DirectoryExists(link, /* follow_symlinks */ false)) {
    CF_EXPECTF(RecursivelyRemoveDirectory(link),
               "Failed to remove legacy directory \"{}\"", link);
  }
  if (FileExists(link, /* follow_symlinks */ false)) {
    CF_EXPECTF(RemoveFile(link), "Failed to remove file \"{}\": {}", link,
               std::strerror(errno));
  }
  CF_EXPECTF(symlink(target.c_str(), link.c_str()) == 0,
             "symlink(\"{}\", \"{}\") failed: {}", target, link,
             std::strerror(errno));
  return {};
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

Result<void> UpdateWebrtcDeviceId(cvd_common::Args& args,
                                  const selector::LocalInstanceGroup& group) {
  std::string flag_value;
  std::vector<Flag> webrtc_device_id_flag{
      GflagsCompatFlag("webrtc_device_id", flag_value)};
  // ConsumeFlags modifies args, a copy is needed because only the presence
  // of the flag is being tested.
  std::vector<std::string> copied_args{args};
  CF_EXPECT(ConsumeFlags(webrtc_device_id_flag, copied_args));

  if (!flag_value.empty()) {
    return {};
  }

  CF_EXPECT(!group.GroupName().empty());
  std::vector<std::string> device_name_list;
  for (const auto& instance : group.Instances()) {
    std::string device_name =
        group.GroupName() + "-" + instance.name();
    device_name_list.push_back(device_name);
  }
  args.push_back("--webrtc_device_id=" +
                 android::base::Join(device_name_list, ","));
  return {};
}

/*
 * 1. Remove --num_instances, --instance_nums, --base_instance_num if any.
 * 2. If the ids are consecutive and ordered, add:
 *   --base_instance_num=min --num_instances=ids.size()
 * 3. If not, --instance_nums=<ids>
 *
 */
static Result<void> UpdateInstanceArgs(
    cvd_common::Args& args, const selector::LocalInstanceGroup& group) {
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
  if (!FileExists(config_from_home)) {
    return {};
  }
  CF_EXPECT(EnsureSymlink(config_from_home,
                          group_home_dir + "/.cuttlefish_config.json"));
  return {};
}

}  // namespace

class CvdStartCommandHandler : public CvdServerHandler {
 public:
  CvdStartCommandHandler(InstanceManager& instance_manager,
                         HostToolTargetManager& host_tool_target_manager,
                         CommandSequenceExecutor& command_executor)
      : instance_manager_(instance_manager),
        host_tool_target_manager_(host_tool_target_manager),
        status_fetcher_(instance_manager_, host_tool_target_manager_),
        // TODO: b/300476262 - Migrate to using local instances rather than
        // constructor-injected ones
        command_executor_(command_executor),
        sub_action_ended_(false) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  std::vector<std::string> CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  Result<cvd::Response> LaunchDevice(Command command,
                                     selector::LocalInstanceGroup& group,
                                     const cvd_common::Envs& envs,
                                     const RequestWithStdio& request);

  Result<cvd::Response> LaunchDeviceInterruptible(
      Command command, selector::LocalInstanceGroup& group,
      const cvd_common::Envs& envs, const RequestWithStdio& request);

  Result<Command> ConstructCvdNonHelpCommand(
      const std::string& bin_file, const selector::LocalInstanceGroup& group,
      const cvd_common::Args& args, const cvd_common::Envs& envs,
      const RequestWithStdio& request);

  struct GroupAndLockFiles {
    selector::LocalInstanceGroup group;
    std::vector<InstanceLockFile> lock_files;
  };

  Result<cvd::Response> FillOutNewInstanceInfo(
      cvd::Response&& response, const selector::LocalInstanceGroup& group);

  Result<void> UpdateArgs(cvd_common::Args& args,
                          const selector::LocalInstanceGroup& group);

  Result<void> UpdateEnvs(cvd_common::Envs& envs,
                          const selector::LocalInstanceGroup& group);

  Result<std::string> FindStartBin(const std::string& android_host_out);

  Result<void> AcloudCompatActions(const selector::LocalInstanceGroup& group,
                                   const cvd_common::Envs& envs,
                                   const RequestWithStdio& request);
  Result<void> CreateSymlinks(const selector::LocalInstanceGroup& group);

  InstanceManager& instance_manager_;
  SubprocessWaiter subprocess_waiter_;
  HostToolTargetManager& host_tool_target_manager_;
  StatusFetcher status_fetcher_;
  CommandSequenceExecutor& command_executor_;
  /*
   * Used by Interrupt() not to call command_executor_.Interrupt()
   *
   * If true, it is guaranteed that the command_executor_ ended the execution.
   * If false, it may or may not be after the command_executor_.Execute()
   */
  std::atomic<bool> sub_action_ended_;
  static const std::array<std::string, 2> supported_commands_;
};

Result<void> CvdStartCommandHandler::AcloudCompatActions(
    const selector::LocalInstanceGroup& group, const cvd_common::Envs& envs,
    const RequestWithStdio& request) {
  // rm -fr "TempDir()/acloud_cvd_temp/local-instance-<i>"
  std::string acloud_compat_home_prefix =
      TempDir() + "/acloud_cvd_temp/local-instance-";
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

  // ln -f -s  [target] [symlink]
  // 1. mkdir -p home
  // 2. ln -f -s android_host_out home/host_bins
  // 3. for each i in ids,
  //     ln -f -s home /tmp/acloud_cvd_temp/local-instance-<i>
  std::vector<MakeRequestForm> request_forms;

  const std::string& home_dir = group.HomeDir();
  const std::string client_pwd =
      request.Message().command_request().working_directory();
  request_forms.push_back(
      {.cmd_args = cvd_common::Args{"mkdir", "-p", home_dir},
       .env = envs,
       .selector_args = cvd_common::Args{},
       .working_dir = client_pwd});
  const std::string& android_host_out = group.HostArtifactsPath();
  request_forms.push_back(
      {.cmd_args = cvd_common::Args{"ln", "-T", "-f", "-s", android_host_out,
                                    home_dir + "/host_bins"},
       .env = envs,
       .selector_args = cvd_common::Args{},
       .working_dir = client_pwd});
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
    request_forms.push_back({
        .cmd_args = cvd_common::Args{"ln", "-T", "-f", "-s", home_dir,
                                     acloud_compat_home},
        .env = envs,
        .selector_args = cvd_common::Args{},
        .working_dir = client_pwd,
    });
  }
  std::vector<cvd::Request> request_protos;
  for (const auto& request_form : request_forms) {
    request_protos.emplace_back(MakeRequest(request_form));
  }
  std::vector<RequestWithStdio> new_requests;
  auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
  CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
  std::vector<SharedFD> dev_null_fds = {dev_null, dev_null, dev_null};
  for (auto& request_proto : request_protos) {
    new_requests.emplace_back(request_proto, dev_null_fds);
  }
  CF_EXPECT(command_executor_.Execute(new_requests, dev_null));
  return {};
}

Result<bool> CvdStartCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(supported_commands_, invocation.command);
}

Result<void> CvdStartCommandHandler::UpdateArgs(
    cvd_common::Args& args, const selector::LocalInstanceGroup& group) {
  CF_EXPECT(UpdateInstanceArgs(args, group));
  CF_EXPECT(UpdateWebrtcDeviceId(args, group));
  // for backward compatibility, older cvd host tools don't accept group_id
  auto has_group_id_flag =
      host_tool_target_manager_
          .ReadFlag({.artifacts_path = group.HostArtifactsPath(),
                     .op = "start",
                     .flag_name = "group_id"})
          .ok();
  if (has_group_id_flag) {
    args.emplace_back("--group_id=" + group.GroupName());
  }
  return {};
}

Result<void> CvdStartCommandHandler::UpdateEnvs(
    cvd_common::Envs& envs, const selector::LocalInstanceGroup& group) {
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
    const std::string& bin_file, const selector::LocalInstanceGroup& group,
    const cvd_common::Args& args, const cvd_common::Envs& envs,
    const RequestWithStdio& request) {
  auto bin_path = group.HostArtifactsPath();
  CF_EXPECTF(PotentiallyHostArtifactsPath(bin_path),
             "ANDROID_HOST_OUT, \"{}\" is not a tool directory", bin_path);
  bin_path.append("/bin/").append(bin_file);
  CF_EXPECT(!group.HomeDir().empty());
  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = group.HomeDir(),
      .args = args,
      .envs = envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = bin_file,
      .in = request.In(),
      // Print everything to stderr, cvd needs to print JSON to stdout which
      // would be unparseable with the subcommand's output.
      .out = request.Err(),
      .err = request.Err()};
  Command non_help_command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  return non_help_command;
}

static std::ostream& operator<<(std::ostream& out, const cvd_common::Args& v) {
  if (v.empty()) {
    return out;
  }
  for (std::size_t i = 0; i < v.size() - 1; i++) {
    out << v.at(i) << " ";
  }
  out << v.back();
  return out;
}

static void ShowLaunchCommand(const Command& command,
                              const cvd_common::Envs& envs) {
  std::stringstream ss;
  std::vector<std::string> interesting_env_names{"HOME",
                                                 kAndroidHostOut,
                                                 kAndroidSoongHostOut,
                                                 "ANDROID_PRODUCT_OUT",
                                                 kCuttlefishInstanceEnvVarName,
                                                 kCuttlefishConfigEnvVarName};
  for (const auto& interesting_env_name : interesting_env_names) {
    if (Contains(envs, interesting_env_name)) {
      ss << interesting_env_name << "=\"" << envs.at(interesting_env_name)
         << "\" ";
    }
  }
  ss << " " << command;
  LOG(INFO) << "launcher command: " << ss.str();
}

Result<std::string> CvdStartCommandHandler::FindStartBin(
    const std::string& android_host_out) {
  auto start_bin = CF_EXPECT(host_tool_target_manager_.ExecBaseName({
      .artifacts_path = android_host_out,
      .op = "start",
  }));
  return start_bin;
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

// For backward compatibility, we add extra symlink in home dir
Result<void> CvdStartCommandHandler::CreateSymlinks(
    const selector::LocalInstanceGroup& group) {
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
  // The config file needs to be copied instead of symlinked because when the
  // group is removed the original file will be deleted leaving the symlink
  // dangling. The config file in the home directory is used by
  // cvd_internal_start to persist the user's choice for
  // -report_anonymous_usage_stats.
  CF_EXPECT(
      Copy(group.InstanceDir(group.Instances()[0]) + "/cuttlefish_config.json",
           CF_EXPECT(SystemWideUserHome()) + "/.cuttlefish_config.json"),
      "Failed to copy config file to home directory");

  // create cuttlefish_runtime to cuttlefish_runtime.id
  CF_EXPECT_NE(std::numeric_limits<unsigned>::max(), smallest_id,
               "The group did not have any instance, which is not expected.");
  const std::string instance_runtime_dir =
      fmt::format("{}/cuttlefish_runtime.{}", system_wide_home, smallest_id);
  const std::string runtime_dir_link = system_wide_home + "/cuttlefish_runtime";
  CF_EXPECT(EnsureSymlink(instance_runtime_dir, runtime_dir_link));
  return {};
}

Result<cvd::Response> CvdStartCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));

  auto [subcmd, subcmd_args] = ParseInvocation(request.Message());
  CF_EXPECT(!GetConfigPath(subcmd_args).has_value(),
            "The 'start' command doesn't accept --config_file, did you mean "
            "'create'?");

  auto precondition_verified = VerifyPrecondition(request);
  if (!precondition_verified.ok()) {
    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        precondition_verified.error().Message());
    return response;
  }

  cvd_common::Envs envs = request.Envs();
  if (Contains(envs, "HOME")) {
    if (envs.at("HOME").empty()) {
      envs.erase("HOME");
    } else {
      // As the end-user may override HOME, this could be a relative path
      // to client's pwd, or may include "~" which is the client's actual
      // home directory.
      auto client_pwd = request.Message().command_request().working_directory();
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
  CF_EXPECT(Contains(supported_commands_, subcmd),
            "subcmd should be start but is " << subcmd);
  const bool is_help = CF_EXPECT(IsHelpSubcmd(subcmd_args));

  if (is_help) {
    auto it = envs.find(kAndroidHostOut);
    CF_EXPECT(it != envs.end());
    const auto bin = CF_EXPECT(FindStartBin(it->second));

    Command command =
        CF_EXPECT(ConstructCvdHelpCommand(bin, envs, subcmd_args, request));
    ShowLaunchCommand(command, envs);

    CF_EXPECT(subprocess_waiter_.Setup(command.Start()));
    auto infop = CF_EXPECT(subprocess_waiter_.Wait());
    return ResponseFromSiginfo(infop);
  }

  CF_EXPECT(ConsumeDaemonModeFlag(subcmd_args));
  subcmd_args.push_back("--daemon=true");

  auto group =
      CF_EXPECT(SelectGroup(instance_manager_, request),
                "Failed to select group to start, did you mean 'cvd create'?");

  CF_EXPECT(!group.HasActiveInstances(),
            "Selected instance group is already started, use `cvd create` to "
            "create a new one.");

  CF_EXPECT(UpdateArgs(subcmd_args, group));
  CF_EXPECT(UpdateEnvs(envs, group));
  const auto bin = CF_EXPECT(FindStartBin(group.HostArtifactsPath()));
  Command command = CF_EXPECT(
      ConstructCvdNonHelpCommand(bin, group, subcmd_args, envs, request));

  // The instance database needs to be updated if an interrupt is received.
  auto handle_res =
      PushInterruptListener([this, &group](int signal) {
        LOG(WARNING) << strsignal(signal) << " signal received, cleanning up";
        auto interrupt_res = subprocess_waiter_.Interrupt();
        if (!interrupt_res.ok()) {
          LOG(ERROR) << "Failed to stop subprocesses: "
                     << interrupt_res.error().Message();
          LOG(ERROR) << "Devices may still be executing in the background, run "
                        "`cvd reset` to ensure a clean state";
        }

        group.SetAllStates(cvd::INSTANCE_STATE_CANCELLED);
        auto update_res = instance_manager_.UpdateInstanceGroup(group);
        if (!update_res.ok()) {
          LOG(ERROR) << "Failed to update group status: "
                     << update_res.error().Message();
        }
        // It's technically possible for the group's state to be set to
        // "running" before abort has a chance to run, but that can only happen
        // if the instances are indeed running, so it's OK.

        std::abort();
      });
  auto listener_handle = CF_EXPECT(std::move(handle_res));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  auto response = CF_EXPECT(
      LaunchDeviceInterruptible(std::move(command), group, envs, request));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  listener_handle.reset();

  auto group_json = CF_EXPECT(status_fetcher_.FetchGroupStatus(request, group));
  auto serialized_json = group_json.toStyledString();
  CF_EXPECT_EQ(WriteAll(request.Out(), serialized_json),
               (ssize_t)serialized_json.size());

  return FillOutNewInstanceInfo(std::move(response), group);
}

static constexpr char kCollectorFailure[] = R"(
  Consider running:
     cvd reset -y

  cvd start failed. While we should collect run_cvd processes to manually
  clean them up, collecting run_cvd failed.
)";
static constexpr char kStopFailure[] = R"(
  Consider running:
     cvd reset -y

  cvd start failed, and stopping run_cvd processes failed.
)";
static Result<cvd::Response> CvdResetGroup(
    const selector::LocalInstanceGroup& group) {
  auto run_cvd_process_manager = RunCvdProcessManager::Get();
  if (!run_cvd_process_manager.ok()) {
    return CommandResponse(cvd::Status::INTERNAL, kCollectorFailure);
  }
  // We can't run stop_cvd here. It may hang forever, and doesn't make sense
  // to interrupt it.
  const auto& instances = group.Instances();
  CF_EXPECT(!instances.empty());
  const auto& first_instance = instances.front();
  auto stop_result = run_cvd_process_manager->ForcefullyStopGroup(
      /* cvd_server_children_only */ true, first_instance.id());
  if (!stop_result.ok()) {
    return CommandResponse(cvd::Status::INTERNAL, kStopFailure);
  }
  return CommandResponse(cvd::Status::OK, "");
}

Result<cvd::Response> CvdStartCommandHandler::LaunchDevice(
    Command launch_command, selector::LocalInstanceGroup& group,
    const cvd_common::Envs& envs, const RequestWithStdio& request) {
  ShowLaunchCommand(launch_command, envs);

  CF_EXPECT(subprocess_waiter_.Setup(launch_command.Start()));

  auto acloud_compat_action_result = AcloudCompatActions(group, envs, request);
  sub_action_ended_ = true;
  if (!acloud_compat_action_result.ok()) {
    LOG(ERROR) << acloud_compat_action_result.error().FormatForEnv();
    LOG(ERROR) << "AcloudCompatActions() failed"
               << " but continue as they are minor errors.";
  }

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());
  if (infop.si_code != CLD_EXITED || infop.si_status != EXIT_SUCCESS) {
    LOG(INFO) << "Device launch failed, cleaning up";
    // run_cvd processes may be still running in background
    // the order of the following operations should be kept
    auto reset_response = CF_EXPECT(CvdResetGroup(group));
    if (reset_response.status().code() != cvd::Status::OK) {
      return reset_response;
    }
  }
  return ResponseFromSiginfo(infop);
}

Result<cvd::Response> CvdStartCommandHandler::LaunchDeviceInterruptible(
    Command command, selector::LocalInstanceGroup& group,
    const cvd_common::Envs& envs, const RequestWithStdio& request) {
  // cvd_internal_start uses the config from the previous invocation to
  // determine the default value for the -report_anonymous_usage_stats flag so
  // we symlink that to the group's home directory, this link will be
  // overwritten later by cvd_internal_start itself.
  auto symlink_config_res = SymlinkPreviousConfig(group.HomeDir());
  if (!symlink_config_res.ok()) {
    LOG(ERROR) << "Failed to symlink the config file at system wide home: "
               << symlink_config_res.error().Message();
  }
  auto start_res = LaunchDevice(std::move(command), group, envs, request);
  if (!start_res.ok() || start_res->status().code() != cvd::Status::OK) {
    group.SetAllStates(cvd::INSTANCE_STATE_BOOT_FAILED);
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
    return start_res;
  }

  auto& response = *start_res;
  if (!response.has_status() || response.status().code() != cvd::Status::OK) {
    return response;
  }

  // For backward compatibility, we add extra symlink in system wide home
  // when HOME is NOT overridden and selector flags are NOT given.
  auto is_default_group =
      StringFromEnv("HOME", "") == CF_EXPECT(SystemWideUserHome()) &&
      request.Message().command_request().selector_opts().args().empty();

  if (is_default_group) {
    auto symlink_res = CreateSymlinks(group);
    if (!symlink_res.ok()) {
      LOG(ERROR) << "Failed to create symlinks for default group: "
                 << symlink_res.error().FormatForEnv();
    }
  }

  return response;
}

Result<cvd::Response> CvdStartCommandHandler::FillOutNewInstanceInfo(
    cvd::Response&& response, const selector::LocalInstanceGroup& group) {
  auto new_response = std::move(response);
  auto& command_response = *(new_response.mutable_command_response());
  auto& instance_group_info =
      *(CF_EXPECT(command_response.mutable_instance_group_info()));
  instance_group_info.set_group_name(group.GroupName());
  instance_group_info.add_home_directories(group.HomeDir());
  for (const auto& instance : group.Instances()) {
    auto* new_entry = CF_EXPECT(instance_group_info.add_instances());
    new_entry->set_name(instance.name());
    new_entry->set_instance_id(instance.id());
  }
  return new_response;
}

std::vector<std::string> CvdStartCommandHandler::CmdList() const {
  std::vector<std::string> subcmd_list;
  subcmd_list.reserve(supported_commands_.size());
  for (const auto& cmd : supported_commands_) {
    subcmd_list.emplace_back(cmd);
  }
  return subcmd_list;
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

const std::array<std::string, 2> CvdStartCommandHandler::supported_commands_{
    "start", "launch_cvd"};

std::unique_ptr<CvdServerHandler> NewCvdStartCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager,
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new CvdStartCommandHandler(
      instance_manager, host_tool_target_manager, executor));
}

}  // namespace cuttlefish
