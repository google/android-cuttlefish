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

#include "host/commands/cvd/server_command_start_impl.h"

#include <sys/types.h>

#include <cstdint>
#include <cstdlib>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

Result<bool> CvdStartCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(command_to_binary_map_, invocation.command);
}

CvdStartCommandHandler::PreconditionVerification
CvdStartCommandHandler::VerifyPrecondition(
    const RequestWithStdio& request) const {
  PreconditionVerification verification_result;
  if (!request.Credentials()) {
    verification_result.error_message =
        "ucred is not available while it is necessary.";
    return verification_result;
  }
  if (!Contains(request.Message().command_request().env(),
                "ANDROID_HOST_OUT")) {
    verification_result.error_message =
        "ANDROID_HOST_OUT in client environment is invalid.";
    return verification_result;
  }
  verification_result.is_ok = true;
  return verification_result;
}

Result<Command> CvdStartCommandHandler::ConstructCvdNonHelpCommand(
    const std::string& bin_file, const selector::GroupCreationInfo& group_info,
    const RequestWithStdio& request) {
  const auto bin_path = group_info.host_artifacts_path + "/bin/" + bin_file;
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
    const std::string& subcmd, const std::vector<std::string>& subcmd_args,
    const Envs& envs, const RequestWithStdio& request) {
  using CreationAnalyzerParam =
      selector::CreationAnalyzer::CreationAnalyzerParam;
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  const auto selector_args = ConvertProtoArguments(selector_opts.args());
  CreationAnalyzerParam analyzer_param{
      .cmd_args = subcmd_args, .envs = envs, .selector_args = selector_args};
  auto cred = CF_EXPECT(request.Credentials());
  auto group_creation_info =
      CF_EXPECT(instance_manager_.Analyze(subcmd, analyzer_param, cred));
  return group_creation_info;
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
  Envs envs = ConvertProtoMap(request.Message().command_request().env());

  // update DB if not help
  // collect group creation infos
  auto [subcmd, subcmd_args] = ParseInvocation(request.Message());
  CF_EXPECT(subcmd == "start", "subcmd should be start but is " << subcmd);
  const bool is_help = HasHelpOpts(subcmd_args);
  const auto bin = command_to_binary_map_.at(subcmd);

  std::optional<selector::GroupCreationInfo> group_creation_info;
  if (!is_help) {
    group_creation_info =
        CF_EXPECT(GetGroupCreationInfo(subcmd, subcmd_args, envs, request));
    CF_EXPECT(UpdateInstanceDatabase(uid, *group_creation_info));
  }

  Command command =
      is_help
          ? CF_EXPECT(ConstructCvdHelpCommand(bin, envs, subcmd_args, request))
          : CF_EXPECT(
                ConstructCvdNonHelpCommand(bin, *group_creation_info, request));

  const bool should_wait =
      (request.Message().command_request().wait_behavior() !=
       cvd::WAIT_BEHAVIOR_START);
  FireCommand(std::move(command), should_wait);
  if (!should_wait) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }
  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());
  if (infop.si_code != CLD_EXITED || infop.si_status != EXIT_SUCCESS) {
    if (!is_help) {
      instance_manager_.RemoveInstanceGroup(uid, group_creation_info->home);
    }
  }
  return ResponseFromSiginfo(infop);
}

Result<void> CvdStartCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
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

const std::map<std::string, std::string>
    CvdStartCommandHandler::command_to_binary_map_ = {
        {"start", kStartBin},
        {"launch_cvd", kStartBin},
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
