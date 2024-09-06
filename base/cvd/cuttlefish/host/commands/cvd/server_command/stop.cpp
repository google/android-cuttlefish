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

#include "host/commands/cvd/server_command/stop.h"

#include <sys/types.h>

#include <functional>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/group_selector.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Run cvd stop --help for command description";

}  // namespace

class CvdStopCommandHandler : public CvdServerHandler {
 public:
  CvdStopCommandHandler(InstanceManager& instance_manager,
                        HostToolTargetManager& host_tool_target_manager);

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  cvd_common::Args CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  Result<cvd::Response> HandleHelpCmd(const RequestWithStdio& request);
  Result<std::string> GetBin(const std::string& host_artifacts_path) const;
  // whether the "bin" is cvd bins like stop_cvd or not (e.g. ln, ls, mkdir)
  // The information to fire the command might be different. This information
  // is about what the executable binary is and how to find it.
  struct BinPathInfo {
    std::string bin_;
    std::string bin_path_;
  };
  Result<BinPathInfo> CvdHelpBinPath(const std::string& subcmd,
                                     const cvd_common::Envs& envs) const;

  InstanceManager& instance_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  using BinGeneratorType = std::function<Result<std::string>(
      const std::string& host_artifacts_path)>;
};

CvdStopCommandHandler::CvdStopCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager)
    : instance_manager_(instance_manager),
      host_tool_target_manager_(host_tool_target_manager) {}

Result<bool> CvdStopCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(CmdList(), invocation.command);
}

Result<cvd::Response> CvdStopCommandHandler::HandleHelpCmd(
    const RequestWithStdio& request) {
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());
  cvd_common::Envs envs = request.Envs();

  const auto [bin, bin_path] = CF_EXPECT(CvdHelpBinPath(subcmd, envs));

  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = CF_EXPECT(SystemWideUserHome()),
      .args = cmd_args,
      .envs = envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = bin,
      .null_stdio = request.IsNullIo()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  siginfo_t infop;
  command.Start().Wait(&infop, WEXITED);

  return ResponseFromSiginfo(infop);
}

Result<cvd::Response> CvdStopCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  if (CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    return CF_EXPECT(HandleHelpCmd(request));
  }

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return CF_EXPECT(NoGroupResponse(request));
  }
  cvd_common::Envs envs = request.Envs();
  const auto selector_args = request.SelectorArgs();

  auto group = CF_EXPECT(SelectGroup(instance_manager_, request));
  CF_EXPECT(group.HasActiveInstances(), "Selected group is not running");

  auto android_host_out = group.HostArtifactsPath();
  auto bin = CF_EXPECT(GetBin(android_host_out));

  ConstructCommandParam construct_cmd_param{
      .bin_path = ConcatToString(android_host_out, "/bin/", bin),
      .home = group.HomeDir(),
      .args = cmd_args,
      .envs = envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = bin,
      .null_stdio = request.IsNullIo()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  siginfo_t infop;
  command.Start().Wait(&infop, WEXITED);
  auto response = ResponseFromSiginfo(infop);

  if (response.status().code() == cvd::Status::OK) {
    group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  }

  return response;
}

std::vector<std::string> CvdStopCommandHandler::CmdList() const {
  return {"stop", "stop_cvd"};
}

Result<std::string> CvdStopCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdStopCommandHandler::ShouldInterceptHelp() const { return false; }

Result<std::string> CvdStopCommandHandler::DetailedHelp(
    std::vector<std::string>& arguments) const {
  return "Run cvd stop --help for full help text";
}

Result<CvdStopCommandHandler::BinPathInfo>
CvdStopCommandHandler::CvdHelpBinPath(const std::string& subcmd,
                                      const cvd_common::Envs& envs) const {
  auto tool_dir_path = CF_EXPECT(AndroidHostPath(envs));
  auto bin_path_base = CF_EXPECT(GetBin(tool_dir_path));
  // no need of executable directory. Will look up by PATH
  // bin_path_base is like ln, mkdir, etc.
  return BinPathInfo{
      .bin_ = bin_path_base,
      .bin_path_ = tool_dir_path.append("/bin/").append(bin_path_base),
  };
}

Result<std::string> CvdStopCommandHandler::GetBin(
    const std::string& host_artifacts_path) const {
  std::string calculated_bin_name =
      CF_EXPECT(host_tool_target_manager_.ExecBaseName(
          {.artifacts_path = host_artifacts_path, .op = "stop"}));
  return calculated_bin_name;
}

std::unique_ptr<CvdServerHandler> NewCvdStopCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdStopCommandHandler(instance_manager, host_tool_target_manager));
}

}  // namespace cuttlefish

