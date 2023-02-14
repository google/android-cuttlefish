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

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class CvdStartCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdStartCommandHandler(
      InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter,
      HostToolTargetManager& host_tool_target_manager))
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter),
        host_tool_target_manager_(host_tool_target_manager) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  std::vector<std::string> CmdList() const override;

 private:
  Result<void> UpdateInstanceDatabase(
      const uid_t uid, const selector::GroupCreationInfo& group_creation_info);
  Result<void> FireCommand(Command&& command, const bool wait);
  bool HasHelpOpts(const cvd_common::Args& args) const;

  Result<Command> ConstructCvdNonHelpCommand(
      const std::string& bin_file,
      const selector::GroupCreationInfo& group_info,
      const RequestWithStdio& request);

  // call this only if !is_help
  Result<selector::GroupCreationInfo> GetGroupCreationInfo(
      const std::string& start_bin, const std::string& subcmd,
      const cvd_common::Args& subcmd_args, const cvd_common::Envs& envs,
      const RequestWithStdio& request);

  Result<cvd::Response> FillOutNewInstanceInfo(
      cvd::Response&& response,
      const selector::GroupCreationInfo& group_creation_info);

  struct UpdatedArgsAndEnvs {
    cvd_common::Args args;
    cvd_common::Envs envs;
  };
  Result<UpdatedArgsAndEnvs> UpdateInstanceArgsAndEnvs(
      cvd_common::Args&& args, cvd_common::Envs&& envs,
      const std::vector<selector::PerInstanceInfo>& instances,
      const std::string& artifacts_path, const std::string& start_bin);

  static Result<std::vector<std::string>> UpdateWebrtcDeviceId(
      std::vector<std::string>&& args, const std::string& group_name,
      const std::vector<selector::PerInstanceInfo>& per_instance_info);

  Result<selector::GroupCreationInfo> UpdateArgsAndEnvs(
      selector::GroupCreationInfo&& old_group_info,
      const std::string& start_bin);

  Result<std::string> FindStartBin(const std::string& android_host_out);

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  HostToolTargetManager& host_tool_target_manager_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static const std::array<std::string, 2> supported_commands_;
};

Result<bool> CvdStartCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(supported_commands_, invocation.command);
}

Result<CvdStartCommandHandler::UpdatedArgsAndEnvs>
CvdStartCommandHandler::UpdateInstanceArgsAndEnvs(
    cvd_common::Args&& args, cvd_common::Envs&& envs,
    const std::vector<selector::PerInstanceInfo>& instances,
    const std::string& artifacts_path, const std::string& start_bin) {
  std::vector<unsigned> ids;
  ids.reserve(instances.size());
  for (const auto& instance : instances) {
    ids.emplace_back(instance.instance_id_);
  }

  cvd_common::Args new_args{std::move(args)};
  std::string old_instance_nums;
  std::string old_num_instances;
  std::string old_base_instance_num;

  std::vector<Flag> instance_id_flags{
      GflagsCompatFlag("instance_nums", old_instance_nums),
      GflagsCompatFlag("num_instances", old_num_instances),
      GflagsCompatFlag("base_instance_num", old_base_instance_num)};
  // discard old ones
  ParseFlags(instance_id_flags, new_args);

  auto check_flag = [artifacts_path, start_bin,
                     this](const std::string& flag_name) -> Result<void> {
    CF_EXPECT(
        host_tool_target_manager_.ReadFlag({.artifacts_path = artifacts_path,
                                            .op = "start",
                                            .flag_name = flag_name}));
    return {};
  };
  auto max = *(std::max_element(ids.cbegin(), ids.cend()));
  auto min = *(std::min_element(ids.cbegin(), ids.cend()));

  const bool is_consecutive = ((max - min) == (ids.size() - 1));
  const bool is_sorted = std::is_sorted(ids.begin(), ids.end());

  if (!is_consecutive || !is_sorted) {
    std::string flag_value = android::base::Join(ids, ",");
    CF_EXPECT(check_flag("instance_nums"));
    new_args.emplace_back("--instance_nums=" + flag_value);
    return UpdatedArgsAndEnvs{.args = std::move(new_args),
                              .envs = std::move(envs)};
  }

  // sorted and consecutive, so let's use old flags
  // like --num_instances and --base_instance_num
  if (ids.size() > 1) {
    CF_EXPECT(check_flag("num_instances"),
              "--num_instances is not supported but multi-tenancy requested.");
    new_args.emplace_back("--num_instances=" + std::to_string(ids.size()));
  }
  cvd_common::Envs new_envs{std::move(envs)};
  if (check_flag("base_instance_num").ok()) {
    new_args.emplace_back("--base_instance_num=" + std::to_string(min));
  }
  new_envs[kCuttlefishInstanceEnvVarName] = std::to_string(min);
  return UpdatedArgsAndEnvs{.args = std::move(new_args),
                            .envs = std::move(new_envs)};
}

/*
 * Adds --webrtc_device_id when necessary to cmd_args_
 */
Result<std::vector<std::string>> CvdStartCommandHandler::UpdateWebrtcDeviceId(
    std::vector<std::string>&& args, const std::string& group_name,
    const std::vector<selector::PerInstanceInfo>& per_instance_info) {
  std::vector<std::string> new_args{std::move(args)};
  std::string flag_value;
  std::vector<Flag> webrtc_device_id_flag{
      GflagsCompatFlag("webrtc_device_id", flag_value)};
  std::vector<std::string> copied_args{new_args};
  CF_EXPECT(ParseFlags(webrtc_device_id_flag, copied_args));

  if (!flag_value.empty()) {
    return new_args;
  }

  CF_EXPECT(!group_name.empty());
  std::vector<std::string> device_name_list;
  device_name_list.reserve(per_instance_info.size());
  for (const auto& instance : per_instance_info) {
    const auto& per_instance_name = instance.per_instance_name_;
    std::string device_name{group_name};
    device_name.append("-").append(per_instance_name);
    device_name_list.emplace_back(device_name);
  }
  // take --webrtc_device_id flag away
  new_args = std::move(copied_args);
  new_args.emplace_back("--webrtc_device_id=" +
                        android::base::Join(device_name_list, ","));
  return new_args;
}

Result<Command> CvdStartCommandHandler::ConstructCvdNonHelpCommand(
    const std::string& bin_file, const selector::GroupCreationInfo& group_info,
    const RequestWithStdio& request) {
  auto bin_path = group_info.host_artifacts_path;
  bin_path.append("/bin/").append(bin_file);
  CF_EXPECT(!group_info.home.empty());
  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = group_info.home,
      .args = group_info.args,
      .envs = group_info.envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = bin_file,
      .in = request.In(),
      .out = request.Out(),
      .err = request.Err()};
  Command non_help_command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  return non_help_command;
}

// call this only if !is_help
Result<selector::GroupCreationInfo>
CvdStartCommandHandler::GetGroupCreationInfo(
    const std::string& start_bin, const std::string& subcmd,
    const std::vector<std::string>& subcmd_args, const cvd_common::Envs& envs,
    const RequestWithStdio& request) {
  using CreationAnalyzerParam =
      selector::CreationAnalyzer::CreationAnalyzerParam;
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());
  CreationAnalyzerParam analyzer_param{
      .cmd_args = subcmd_args, .envs = envs, .selector_args = selector_args};
  auto cred = CF_EXPECT(request.Credentials());
  auto group_creation_info =
      CF_EXPECT(instance_manager_.Analyze(subcmd, analyzer_param, cred));
  auto final_group_creation_info =
      CF_EXPECT(UpdateArgsAndEnvs(std::move(group_creation_info), start_bin));
  return final_group_creation_info;
}

Result<selector::GroupCreationInfo> CvdStartCommandHandler::UpdateArgsAndEnvs(
    selector::GroupCreationInfo&& old_group_info,
    const std::string& start_bin) {
  selector::GroupCreationInfo group_creation_info = std::move(old_group_info);
  // update instance related-flags, envs
  const auto& instances = group_creation_info.instances;
  const auto& host_artifacts_path = group_creation_info.host_artifacts_path;
  auto [new_args, new_envs] = CF_EXPECT(UpdateInstanceArgsAndEnvs(
      std::move(group_creation_info.args), std::move(group_creation_info.envs),
      instances, host_artifacts_path, start_bin));
  group_creation_info.args = std::move(new_args);
  group_creation_info.envs = std::move(new_envs);

  auto webrtc_device_id_flag = host_tool_target_manager_.ReadFlag(
      {.artifacts_path = group_creation_info.host_artifacts_path,
       .op = "start",
       .flag_name = "webrtc_device_id"});
  if (webrtc_device_id_flag.ok()) {
    group_creation_info.args = CF_EXPECT(UpdateWebrtcDeviceId(
        std::move(group_creation_info.args), group_creation_info.group_name,
        group_creation_info.instances));
  }

  group_creation_info.envs["HOME"] = group_creation_info.home;
  group_creation_info.envs[kAndroidHostOut] =
      group_creation_info.host_artifacts_path;
  /* b/253644566
   *
   * Old branches used kAndroidSoongHostOut instead of kAndroidHostOut
   */
  group_creation_info.envs[kAndroidSoongHostOut] =
      group_creation_info.host_artifacts_path;
  group_creation_info.envs[kCvdMarkEnv] = "true";
  return group_creation_info;
}

static std::ostream& operator<<(std::ostream& out, const cvd_common::Args& v) {
  if (v.empty()) {
    return out;
  }
  for (int i = 0; i < v.size() - 1; i++) {
    out << v.at(i) << " ";
  }
  out << v.back();
  return out;
}

static void ShowLaunchCommand(const std::string& bin,
                              const cvd_common::Args& args,
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
  ss << " " << bin << " " << args;
  LOG(ERROR) << "launcher command: " << ss.str();
}

static void ShowLaunchCommand(const std::string& bin,
                              selector::GroupCreationInfo& group_info) {
  ShowLaunchCommand(bin, group_info.args, group_info.envs);
}

Result<std::string> CvdStartCommandHandler::FindStartBin(
    const std::string& android_host_out) {
  auto start_bin = CF_EXPECT(host_tool_target_manager_.ExecBaseName({
      .artifacts_path = android_host_out,
      .op = "start",
  }));
  return start_bin;
}

Result<cvd::Response> CvdStartCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  if (interrupted_) {
    return CF_ERR("Interrupted");
  }
  CF_EXPECT(CanHandle(request));

  cvd::Response response;
  response.mutable_command_response();

  auto [meets_precondition, error_message] = VerifyPrecondition(request);
  if (!meets_precondition) {
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(error_message);
    return response;
  }

  const uid_t uid = request.Credentials()->uid;
  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  if (Contains(envs, "HOME")) {
    // As the end-user may override HOME, this could be a relative path
    // to client's pwd, or may include "~" which is the client's actual
    // home directory.
    auto client_pwd = request.Message().command_request().working_directory();
    envs["HOME"] = CF_EXPECT(ClientAbsolutePath(envs["HOME"], uid, client_pwd));
  }
  CF_EXPECT(Contains(envs, kAndroidHostOut));
  const auto bin = CF_EXPECT(FindStartBin(envs.at(kAndroidHostOut)));

  // update DB if not help
  // collect group creation infos
  auto [subcmd, subcmd_args] = ParseInvocation(request.Message());
  CF_EXPECT(Contains(supported_commands_, subcmd),
            "subcmd should be start but is " << subcmd);
  const bool is_help = HasHelpOpts(subcmd_args);

  std::optional<selector::GroupCreationInfo> group_creation_info;
  if (!is_help) {
    group_creation_info = CF_EXPECT(
        GetGroupCreationInfo(bin, subcmd, subcmd_args, envs, request));
    CF_EXPECT(UpdateInstanceDatabase(uid, *group_creation_info));
    response = CF_EXPECT(
        FillOutNewInstanceInfo(std::move(response), *group_creation_info));
  }

  Command command =
      is_help
          ? CF_EXPECT(ConstructCvdHelpCommand(bin, envs, subcmd_args, request))
          : CF_EXPECT(
                ConstructCvdNonHelpCommand(bin, *group_creation_info, request));

  if (!is_help) {
    CF_EXPECT(
        group_creation_info != std::nullopt,
        "group_creation_info should be nullopt only when --help is given.");
  }

  if (is_help) {
    ShowLaunchCommand(command.Executable(), subcmd_args, envs);
  } else {
    ShowLaunchCommand(command.Executable(), *group_creation_info);
  }

  const bool should_wait =
      (request.Message().command_request().wait_behavior() !=
       cvd::WAIT_BEHAVIOR_START);
  FireCommand(std::move(command), should_wait);
  if (!should_wait) {
    response.mutable_status()->set_code(cvd::Status::OK);
    if (!is_help) {
      response = CF_EXPECT(
          FillOutNewInstanceInfo(std::move(response), *group_creation_info));
    }
    return response;
  }
  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());
  if (infop.si_code != CLD_EXITED || infop.si_status != EXIT_SUCCESS) {
    if (!is_help) {
      instance_manager_.RemoveInstanceGroup(uid, group_creation_info->home);
    }
  }
  auto final_response = ResponseFromSiginfo(infop);
  if (is_help || !final_response.has_status() ||
      final_response.status().code() != cvd::Status::OK) {
    return final_response;
  }
  // group_creation_info is nullopt only if is_help is false
  return FillOutNewInstanceInfo(std::move(final_response),
                                *group_creation_info);
}

Result<void> CvdStartCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<cvd::Response> CvdStartCommandHandler::FillOutNewInstanceInfo(
    cvd::Response&& response,
    const selector::GroupCreationInfo& group_creation_info) {
  auto new_response = std::move(response);
  auto& command_response = *(new_response.mutable_command_response());
  auto& instance_group_info =
      *(CF_EXPECT(command_response.mutable_instance_group_info()));
  instance_group_info.set_group_name(group_creation_info.group_name);
  instance_group_info.add_home_directories(group_creation_info.home);
  for (const auto& per_instance_info : group_creation_info.instances) {
    auto* new_entry = CF_EXPECT(instance_group_info.add_instances());
    new_entry->set_name(per_instance_info.per_instance_name_);
    new_entry->set_instance_id(per_instance_info.instance_id_);
  }
  return new_response;
}

Result<void> CvdStartCommandHandler::UpdateInstanceDatabase(
    const uid_t uid, const selector::GroupCreationInfo& group_creation_info) {
  CF_EXPECT(instance_manager_.SetInstanceGroup(uid, group_creation_info),
            group_creation_info.home
                << " is already taken so can't create new instance.");
  return {};
}

Result<void> CvdStartCommandHandler::FireCommand(Command&& command,
                                                 const bool wait) {
  SubprocessOptions options;
  if (!wait) {
    options.ExitWithParent(false);
  }
  CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));
  return {};
}

bool CvdStartCommandHandler::HasHelpOpts(
    const std::vector<std::string>& args) const {
  return IsHelpSubcmd(args);
}

std::vector<std::string> CvdStartCommandHandler::CmdList() const {
  std::vector<std::string> subcmd_list;
  subcmd_list.reserve(supported_commands_.size());
  for (const auto& cmd : supported_commands_) {
    subcmd_list.emplace_back(cmd);
  }
  return subcmd_list;
}

const std::array<std::string, 2> CvdStartCommandHandler::supported_commands_{
    "start", "launch_cvd"};

fruit::Component<
    fruit::Required<InstanceManager, SubprocessWaiter, HostToolTargetManager>>
cvdStartCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdStartCommandHandler>();
}

}  // namespace cuttlefish
