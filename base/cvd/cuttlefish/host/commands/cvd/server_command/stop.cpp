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

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/group_selector.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/interruptible_terminal.h"
#include "host/commands/cvd/selector/selector_constants.h"
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
  Result<selector::LocalInstanceGroup> PromptUserForGroup(
      const RequestWithStdio& request, const cvd_common::Envs& envs,
      const cvd_common::Args& selector_args);
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
      .in = request.In(),
      .out = request.Out(),
      .err = request.Err()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  siginfo_t infop;
  command.Start().Wait(&infop, WEXITED);

  return ResponseFromSiginfo(infop);
}

Result<selector::LocalInstanceGroup> CvdStopCommandHandler::PromptUserForGroup(
    const RequestWithStdio& request, const cvd_common::Envs& envs,
    const cvd_common::Args& selector_args) {
  // show the menu and let the user choose
  std::vector<selector::LocalInstanceGroup> groups =
      CF_EXPECT(instance_manager_.FindGroups(selector::Queries{}));
  groups.erase(std::remove_if(groups.begin(), groups.end(),
                              [](const auto& group) {
                                return !group.HasActiveInstances();
                              }),
               groups.end());
  GroupSelector selector{.groups = groups};
  auto menu = selector.Menu();

  CF_EXPECT_EQ(WriteAll(request.Out(), menu + "\n"), (ssize_t)menu.size() + 1);
  std::unique_ptr<InterruptibleTerminal> terminal_ =
      std::make_unique<InterruptibleTerminal>(request.In());

  const bool is_tty = request.Err()->IsOpen() && request.Err()->IsATTY();
  while (true) {
    std::string question = "Which instance group would you like to stop?";
    CF_EXPECT_EQ(WriteAll(request.Out(), question), (ssize_t)question.size());

    std::string input_line = CF_EXPECT(terminal_->ReadLine());
    int selection = -1;
    std::string chosen_group_name;
    if (android::base::ParseInt(input_line, &selection)) {
      const int n_groups = selector.groups.size();
      if (n_groups <= selection || selection < 0) {
        std::string out_of_range = fmt::format(
            "\n  Selection {}{}{} is beyond the range {}[0, {}]{}\n\n",
            TerminalColor(is_tty, TerminalColors::kBoldRed), selection,
            TerminalColor(is_tty, TerminalColors::kReset),
            TerminalColor(is_tty, TerminalColors::kCyan), n_groups - 1,
            TerminalColor(is_tty, TerminalColors::kReset));
        CF_EXPECT_EQ(WriteAll(request.Err(), out_of_range),
                     (ssize_t)out_of_range.size());
        continue;
      }
      chosen_group_name = selector.groups[selection].GroupName();
    } else {
      chosen_group_name = android::base::Trim(input_line);
    }

    InstanceManager::Queries extra_queries{
        {selector::kGroupNameField, chosen_group_name}};
    auto instance_group_result =
        instance_manager_.SelectGroup(selector_args, envs, extra_queries);
    if (instance_group_result.ok()) {
      return instance_group_result;
    }
    std::string cannot_find_group_name = fmt::format(
        "\n  Failed to find a group whose name is {}\"{}\"{}\n\n",
        TerminalColor(is_tty, TerminalColors::kBoldRed), chosen_group_name,
        TerminalColor(is_tty, TerminalColors::kReset));
    CF_EXPECT_EQ(WriteAll(request.Err(), cannot_find_group_name),
                 (ssize_t)cannot_find_group_name.size());
  }
}

Result<cvd::Response> CvdStopCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  auto precondition_verified = VerifyPrecondition(request);
  if (!precondition_verified.ok()) {
    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        precondition_verified.error().Message());
    return response;
  }

  if (CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    return CF_EXPECT(HandleHelpCmd(request));
  }

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return CF_EXPECT(NoGroupResponse(request));
  }
  cvd_common::Envs envs = request.Envs();
  const auto selector_args = request.SelectorArgs();
  CF_EXPECT(Contains(envs, kAndroidHostOut) &&
            DirectoryExists(envs.at(kAndroidHostOut)));

  auto group_selection_result =
      instance_manager_.SelectGroup(selector_args, envs);
  if (!group_selection_result.ok()) {
    if (!request.In()->IsOpen() || !request.In()->IsATTY()) {
      return CF_EXPECT(NoTTYResponse(request));
    }
    group_selection_result = PromptUserForGroup(request, envs, selector_args);
  }

  auto group = CF_EXPECT(std::move(group_selection_result));

  auto android_host_out = group.HostArtifactsPath();
  auto bin = CF_EXPECT(GetBin(android_host_out));

  ConstructCommandParam construct_cmd_param{
      .bin_path = ConcatToString(android_host_out, "/bin/", bin),
      .home = group.HomeDir(),
      .args = cmd_args,
      .envs = envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = bin,
      .in = request.In(),
      .out = request.Out(),
      .err = request.Err()};
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
  CF_EXPECT(Contains(envs, kAndroidHostOut) &&
            DirectoryExists(envs.at(kAndroidHostOut)));
  auto tool_dir_path = envs.at(kAndroidHostOut);
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

