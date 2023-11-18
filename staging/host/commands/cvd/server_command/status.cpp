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

#include "host/commands/cvd/server_command/status.h"

#include <sys/types.h>

#include <functional>
#include <mutex>

#include <android-base/file.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class CvdStatusCommandHandler : public CvdServerHandler {
 public:
  CvdStatusCommandHandler(InstanceManager& instance_manager,
                          HostToolTargetManager& host_tool_target_manager);

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override;

 private:
  struct CommandInvocationInfo {
    std::string command;
    std::string bin;
    std::string bin_path;
    std::string home;
    std::string host_artifacts_path;
    uid_t uid;
    std::vector<std::string> args;
    cvd_common::Envs envs;
  };
  struct ExtractedInfo {
    CommandInvocationInfo invocation_info;
    std::optional<selector::LocalInstanceGroup> group;
  };
  Result<ExtractedInfo> ExtractInfo(const RequestWithStdio& request) const;
  Result<std::string> GetBin(const std::string& host_artifacts_path) const;
  // whether the "bin" is cvd bins like stop_cvd or not (e.g. ln, ls, mkdir)
  // The information to fire the command might be different. This information
  // is about what the executable binary is and how to find it.
  struct BinPathInfo {
    std::string bin_;
    std::string bin_path_;
    std::string host_artifacts_path_;
  };
  Result<BinPathInfo> HelpBinPath(const cvd_common::Envs& envs) const;
  Result<BinPathInfo> NonHelpBinPath(const cvd_common::Envs& envs,
                                     const std::string& home,
                                     const uid_t uid) const;

  Result<bool> IsInstanceStatus(cvd_common::Args selector_args_copy,
                                const cvd_common::Envs& envs) const;
  InstanceManager& instance_manager_;
  SubprocessWaiter subprocess_waiter_;
  HostToolTargetManager& host_tool_target_manager_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> supported_subcmds_;
  selector::SelectorFlags selector_flags_;
};

Result<bool> CvdStatusCommandHandler::IsInstanceStatus(
    cvd_common::Args selector_args_copy, const cvd_common::Envs& envs) const {
  auto instance_name_flag =
      CF_EXPECT(selector_flags_.GetFlag(selector_flags_.kInstanceName));
  auto instance_name_opt =
      CF_EXPECT(instance_name_flag.FilterFlag(selector_args_copy));
  if (instance_name_opt) {
    return true;
  }

  if (Contains(envs, kCuttlefishInstanceEnvVarName)) {
    auto id_tokens =
        android::base::Tokenize(envs.at(kCuttlefishInstanceEnvVarName), ",");
    if (id_tokens.size() == 1) {
      return true;
    }
  }
  return false;
}

CvdStatusCommandHandler::CvdStatusCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager)
    : instance_manager_(instance_manager),
      host_tool_target_manager_(host_tool_target_manager),
      supported_subcmds_{"status", "cvd_status"},
      selector_flags_{selector::SelectorFlags::New()} {}

Result<bool> CvdStatusCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(supported_subcmds_, invocation.command);
}

Result<void> CvdStatusCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<cvd::Response> CvdStatusCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(request.Credentials() != std::nullopt);

  cvd::Response response;
  response.mutable_command_response();

  auto precondition_verified = VerifyPrecondition(request);
  if (!precondition_verified.ok()) {
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        precondition_verified.error().Message());
    return response;
  }
  auto [invocation_info, group_opt] = CF_EXPECT(ExtractInfo(request));

  ConstructCommandParam construct_cmd_param{
      .bin_path = invocation_info.bin_path,
      .home = invocation_info.home,
      .args = invocation_info.args,
      .envs = invocation_info.envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = invocation_info.bin,
      .in = request.In(),
      .out = request.Out(),
      .err = request.Err()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  SubprocessOptions options;
  CF_EXPECT_NE(request.Message().command_request().wait_behavior(),
               cvd::WAIT_BEHAVIOR_START,
               "cvd status shouldn't be cvd::WAIT_BEHAVIOR_START");
  CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));

  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());

  return ResponseFromSiginfo(infop);
}

std::vector<std::string> CvdStatusCommandHandler::CmdList() const {
  return supported_subcmds_;
}

Result<CvdStatusCommandHandler::BinPathInfo>
CvdStatusCommandHandler::HelpBinPath(const cvd_common::Envs& envs) const {
  auto tool_dir_path = envs.at(kAndroidHostOut);
  if (!DirectoryExists(tool_dir_path + "/bin")) {
    tool_dir_path =
        android::base::Dirname(android::base::GetExecutableDirectory());
  }
  auto bin_path_base = CF_EXPECT(GetBin(tool_dir_path));
  // no need of executable directory. Will look up by PATH
  // bin_path_base is like ln, mkdir, etc.
  return BinPathInfo{
      .bin_ = bin_path_base,
      .bin_path_ = tool_dir_path.append("/bin/").append(bin_path_base),
      .host_artifacts_path_ = envs.at(kAndroidHostOut)};
}

Result<CvdStatusCommandHandler::BinPathInfo>
CvdStatusCommandHandler::NonHelpBinPath(const cvd_common::Envs& envs,
                                        const std::string& home,
                                        const uid_t uid) const {
  std::string host_artifacts_path;
  auto instance_group_result = instance_manager_.FindGroup(
      uid, InstanceManager::Query{selector::kHomeField, home});

  // the dir that "bin/cvd_internal_status" belongs to
  std::string tool_dir_path;
  if (instance_group_result.ok()) {
    host_artifacts_path = instance_group_result->HostArtifactsPath();
    tool_dir_path = host_artifacts_path;
  } else {
    // if the group does not exist (e.g. cvd status --help)
    // falls back here
    host_artifacts_path = envs.at(kAndroidHostOut);
    tool_dir_path = host_artifacts_path;
    if (!DirectoryExists(tool_dir_path + "/bin")) {
      tool_dir_path =
          android::base::Dirname(android::base::GetExecutableDirectory());
    }
  }
  const std::string bin = CF_EXPECT(GetBin(tool_dir_path));
  const std::string bin_path = tool_dir_path.append("/bin/").append(bin);
  CF_EXPECT(FileExists(bin_path));
  return BinPathInfo{.bin_ = bin,
                     .bin_path_ = bin_path,
                     .host_artifacts_path_ = host_artifacts_path};
}

Result<CvdStatusCommandHandler::ExtractedInfo>
CvdStatusCommandHandler::ExtractInfo(const RequestWithStdio& request) const {
  auto result_opt = request.Credentials();
  CF_EXPECT(result_opt != std::nullopt);
  const uid_t uid = result_opt->uid;

  auto [subcmd, cmd_args] = ParseInvocation(request.Message());
  CF_EXPECT(Contains(supported_subcmds_, subcmd));

  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());
  CF_EXPECT(Contains(envs, kAndroidHostOut) &&
            DirectoryExists(envs.at(kAndroidHostOut)));

  if (IsHelpSubcmd(cmd_args)) {
    const auto [bin, bin_path, host_artifacts_path] =
        CF_EXPECT(HelpBinPath(envs));
    return ExtractedInfo{
        .invocation_info =
            CommandInvocationInfo{
                .command = subcmd,
                .bin = bin,
                .bin_path = bin_path,
                .home = CF_EXPECT(SystemWideUserHome(uid)),
                .host_artifacts_path = envs.at(kAndroidHostOut),
                .uid = uid,
                .args = cmd_args,
                .envs = envs},
        .group = std::nullopt};
  }

  const auto instance_status_intended =
      CF_EXPECT(IsInstanceStatus(selector_args, envs));
  auto instance_group =
      CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs, uid));
  auto android_host_out = instance_group.HostArtifactsPath();
  auto home = instance_group.HomeDir();
  auto bin = CF_EXPECT(GetBin(android_host_out));
  auto bin_path = ConcatToString(android_host_out, "/bin/", bin);

  // add instance_name=<internal_name>
  if (instance_status_intended) {
    // note that only inside instance_manager_, the data structures are
    // protected by mutex. So, we can't simply get the instances from the
    // instance_group above to iterate over
    auto instance_record =
        CF_EXPECT(instance_manager_.SelectInstance(selector_args, envs, uid));
    auto id = instance_record.InstanceId();
    envs[kCuttlefishInstanceEnvVarName] = std::to_string(id);
  } else {
    if (host_tool_target_manager_
            .ReadFlag({
                .artifacts_path = android_host_out,
                .op = "status",
                .flag_name = "all_instances",
            })
            .ok()) {
      cmd_args.push_back("--all_instances");
    }
  }
  CommandInvocationInfo result = {.command = subcmd,
                                  .bin = bin,
                                  .bin_path = bin_path,
                                  .home = home,
                                  .host_artifacts_path = android_host_out,
                                  .uid = uid,
                                  .args = cmd_args,
                                  .envs = envs};
  result.envs["HOME"] = home;
  return ExtractedInfo{.invocation_info = result, .group = instance_group};
}

Result<std::string> CvdStatusCommandHandler::GetBin(
    const std::string& host_artifacts_path) const {
  return CF_EXPECT(host_tool_target_manager_.ExecBaseName({
      .artifacts_path = host_artifacts_path,
      .op = "status",
  }));
}

std::unique_ptr<CvdServerHandler> NewCvdStatusCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdStatusCommandHandler(instance_manager, host_tool_target_manager));
}

}  // namespace cuttlefish
