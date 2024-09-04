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

#include "host/commands/cvd/server_command/generic.h"

#include <sys/types.h>

#include <functional>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/group_selector.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/interruptible_terminal.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Run cvd <command> --help for command description";

class CvdGenericCommandHandler : public CvdServerHandler {
 public:
  CvdGenericCommandHandler(InstanceManager& instance_manager);

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  cvd_common::Args CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  struct CommandInvocationInfo {
    std::string command;
    std::string bin;
    std::string bin_path;
    std::string home;
    std::string host_artifacts_path;
    std::vector<std::string> args;
    cvd_common::Envs envs;
  };
  enum class UiResponseType : int {
    kNoGroup = 1,        // no group is active
    kNoTTY = 2,          // there are groups to select but no tty for user input
    kUserSelection = 3,  // selector couldn't pick so asked the user
    kCvdServerPick = 4,  // selector picks based on selector flags, env, etc
  };
  struct ExtractedInfo {
    CommandInvocationInfo invocation_info;
    std::optional<selector::LocalInstanceGroup> group;
    bool is_non_help_cvd;
    UiResponseType ui_response_type;
  };
  Result<ExtractedInfo> ExtractInfo(const RequestWithStdio& request);
  Result<std::string> GetBin(const std::string& subcmd) const;
  // whether the "bin" is cvd bins like cvd_host_bugreport or not (e.g. ls,
  // mkdir) The information to fire the command might be different. This
  // information is about what the executable binary is and how to find it.
  struct BinPathInfo {
    std::string bin_;
    std::string bin_path_;
    std::string host_artifacts_path_;
  };
  Result<BinPathInfo> NonCvdBinPath(const std::string& subcmd,
                                    const cvd_common::Envs& envs) const;
  Result<BinPathInfo> CvdHelpBinPath(const std::string& subcmd,
                                     const cvd_common::Envs& envs) const;

  InstanceManager& instance_manager_;
  using BinGeneratorType = std::function<Result<std::string>(
      const std::string& host_artifacts_path)>;
  std::map<std::string, std::string> command_to_binary_map_;
  std::unique_ptr<InterruptibleTerminal> terminal_ = nullptr;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
  static constexpr char kClearBin[] =
      "clear_placeholder";  // Unused, runs CvdClear()
  // Only indicates that host_tool_target_manager_ should generate at runtime
};

CvdGenericCommandHandler::CvdGenericCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager),
      command_to_binary_map_{{"host_bugreport", kHostBugreportBin},
                             {"cvd_host_bugreport", kHostBugreportBin},
                             {"clear", kClearBin}} {}

Result<bool> CvdGenericCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(command_to_binary_map_, invocation.command);
}

Result<cvd::Response> CvdGenericCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));

  cvd::Response response;
  response.mutable_command_response();

  auto precondition_verified = VerifyPrecondition(request);
  if (!precondition_verified.ok()) {
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        precondition_verified.error().Message());
    return response;
  }

  auto [invocation_info, group_opt, is_non_help_cvd, ui_response_type] =
      CF_EXPECT(ExtractInfo(request));

  if (invocation_info.bin == kClearBin) {
    *response.mutable_status() = instance_manager_.CvdClear(request);
    return response;
  }

  // besides the two cases, the rest will be handled by running subprocesses
  if (is_non_help_cvd && ui_response_type == UiResponseType::kNoGroup) {
    return CF_EXPECT(NoGroupResponse(request));
  }
  if (is_non_help_cvd && ui_response_type == UiResponseType::kNoTTY) {
    return CF_EXPECT(NoTTYResponse(request));
  }

  ConstructCommandParam construct_cmd_param{
      .bin_path = invocation_info.bin_path,
      .home = invocation_info.home,
      .args = invocation_info.args,
      .envs = invocation_info.envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = invocation_info.bin,
      .null_stdio = request.IsNullIo()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  siginfo_t infop;
  command.Start().Wait(&infop, WEXITED);

  return ResponseFromSiginfo(infop);
}

std::vector<std::string> CvdGenericCommandHandler::CmdList() const {
  std::vector<std::string> subcmd_list;
  subcmd_list.reserve(command_to_binary_map_.size());
  for (const auto& [cmd, _] : command_to_binary_map_) {
    subcmd_list.emplace_back(cmd);
  }
  return subcmd_list;
}

Result<std::string> CvdGenericCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdGenericCommandHandler::ShouldInterceptHelp() const { return false; }

Result<std::string> CvdGenericCommandHandler::DetailedHelp(
    std::vector<std::string>& arguments) const {
  static constexpr char kDetailedHelpText[] =
      "Run cvd {} --help for full help text";
  std::string replacement = "<command>";
  if (!arguments.empty()) {
    replacement = arguments.front();
  }
  return fmt::format(kDetailedHelpText, replacement);
}

Result<CvdGenericCommandHandler::BinPathInfo>
CvdGenericCommandHandler::NonCvdBinPath(const std::string& subcmd,
                                        const cvd_common::Envs& envs) const {
  auto bin_path_base = CF_EXPECT(GetBin(subcmd));
  // no need of executable directory. Will look up by PATH
  return BinPathInfo{.bin_ = bin_path_base,
                     .bin_path_ = bin_path_base,
                     .host_artifacts_path_ = envs.at(kAndroidHostOut)};
}

Result<CvdGenericCommandHandler::BinPathInfo>
CvdGenericCommandHandler::CvdHelpBinPath(const std::string& subcmd,
                                         const cvd_common::Envs& envs) const {
  auto tool_dir_path = envs.at(kAndroidHostOut);
  if (!DirectoryExists(tool_dir_path + "/bin")) {
    tool_dir_path =
        android::base::Dirname(android::base::GetExecutableDirectory());
  }
  auto bin_path_base = CF_EXPECT(GetBin(subcmd));
  return BinPathInfo{
      .bin_ = bin_path_base,
      .bin_path_ = tool_dir_path.append("/bin/").append(bin_path_base),
      .host_artifacts_path_ = envs.at(kAndroidHostOut)};
}

/*
 * commands like clear
 *  -> bin, bin, system_wide_home, N/A, cmd_args, envs
 *
 * help command
 *  -> android_out/bin, bin, system_wide_home, android_out, cmd_args, envs
 *
 * non-help command
 *  -> group->a/o/bin, bin, group->home, group->android_out, cmd_args, envs
 *
 */
Result<CvdGenericCommandHandler::ExtractedInfo>
CvdGenericCommandHandler::ExtractInfo(const RequestWithStdio& request) {
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());
  CF_EXPECT(Contains(command_to_binary_map_, subcmd));

  cvd_common::Envs envs = request.Envs();
  const auto selector_args = request.SelectorArgs();
  CF_EXPECT(Contains(envs, kAndroidHostOut) &&
            DirectoryExists(envs.at(kAndroidHostOut)));

  if (subcmd == "clear" || CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    const auto [bin, bin_path, host_artifacts_path] =
        (subcmd == "clear") ? CF_EXPECT(NonCvdBinPath(subcmd, envs))
                                     : CF_EXPECT(CvdHelpBinPath(subcmd, envs));
    return ExtractedInfo{
        .invocation_info =
            CommandInvocationInfo{
                .command = subcmd,
                .bin = bin,
                .bin_path = bin_path,
                .home = CF_EXPECT(SystemWideUserHome()),
                .host_artifacts_path = envs.at(kAndroidHostOut),
                .args = cmd_args,
                .envs = envs},
        .group = std::nullopt,
        .is_non_help_cvd = false,
        .ui_response_type = UiResponseType::kCvdServerPick,
    };
  }

  auto instance_group = CF_EXPECT(SelectGroup(instance_manager_, request));
  auto android_host_out = instance_group.HostArtifactsPath();
  auto home = instance_group.HomeDir();
  auto bin = CF_EXPECT(GetBin(subcmd));
  auto bin_path = ConcatToString(android_host_out, "/bin/", bin);
  CommandInvocationInfo result = {.command = subcmd,
                                  .bin = bin,
                                  .bin_path = bin_path,
                                  .home = home,
                                  .host_artifacts_path = android_host_out,
                                  .args = cmd_args,
                                  .envs = envs};
  result.envs["HOME"] = home;
  result.envs[kAndroidHostOut] = android_host_out;

  return ExtractedInfo{
      .invocation_info = result,
      .group = instance_group,
      .is_non_help_cvd = true,
      .ui_response_type = UiResponseType::kCvdServerPick,
  };
}

Result<std::string> CvdGenericCommandHandler::GetBin(
    const std::string& subcmd) const {
  CF_EXPECT(Contains(command_to_binary_map_, subcmd));
  return command_to_binary_map_.at(subcmd);
}

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdGenericCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdGenericCommandHandler(instance_manager));
}

}  // namespace cuttlefish
