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

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
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
#include "host/commands/cvd/selector/selector_constants.h"
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
  Result<std::string> GetBin(const std::string& subcmd,
                             const std::string& host_artifacts_path) const;
  // whether the "bin" is cvd bins like cvd_host_bugreport or not (e.g. ln, ls,
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
  static constexpr char kLnBin[] = "ln";
  static constexpr char kMkdirBin[] = "mkdir";
  static constexpr char kClearBin[] =
      "clear_placeholder";  // Unused, runs CvdClear()
  // Only indicates that host_tool_target_manager_ should generate at runtime
};

CvdGenericCommandHandler::CvdGenericCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager),
      command_to_binary_map_{{"host_bugreport", kHostBugreportBin},
                             {"cvd_host_bugreport", kHostBugreportBin},
                             {"clear", kClearBin},
                             {"mkdir", kMkdirBin},
                             {"ln", kLnBin}} {}

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
    *response.mutable_status() =
        instance_manager_.CvdClear(request.Out(), request.Err());
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
      .in = request.In(),
      .out = request.Out(),
      .err = request.Err()};
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
  // bin_path_base is like ln, mkdir, etc.
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
  auto bin_path_base = CF_EXPECT(GetBin(subcmd, tool_dir_path));
  // no need of executable directory. Will look up by PATH
  // bin_path_base is like ln, mkdir, etc.
  return BinPathInfo{
      .bin_ = bin_path_base,
      .bin_path_ = tool_dir_path.append("/bin/").append(bin_path_base),
      .host_artifacts_path_ = envs.at(kAndroidHostOut)};
}

/*
 * commands like ln, mkdir, clear
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

  std::unordered_set<std::string> non_cvd_op{"clear", "mkdir", "ln"};
  if (Contains(non_cvd_op, subcmd) || CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    const auto [bin, bin_path, host_artifacts_path] =
        Contains(non_cvd_op, subcmd) ? CF_EXPECT(NonCvdBinPath(subcmd, envs))
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

  auto instance_group_result =
      instance_manager_.SelectGroup(selector_args, envs);
  ExtractedInfo extracted_info{
      .invocation_info = CommandInvocationInfo(),
      .group = std::nullopt,
      .is_non_help_cvd = true,
      .ui_response_type = UiResponseType::kCvdServerPick,
  };
  if (!instance_group_result.ok()) {
    if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
      extracted_info.ui_response_type = UiResponseType::kNoGroup;
      return extracted_info;
    }

    if (!request.In()->IsOpen() || !request.In()->IsATTY()) {
      // can't take the user input
      extracted_info.ui_response_type = UiResponseType::kNoTTY;
      return extracted_info;
    }

    extracted_info.ui_response_type = UiResponseType::kUserSelection;
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

    CF_EXPECT_EQ(WriteAll(request.Out(), menu + "\n"),
                 (ssize_t)menu.size() + 1);
    terminal_ = std::make_unique<InterruptibleTerminal>(request.In());

    const bool is_tty = request.Err()->IsOpen() && request.Err()->IsATTY();
    while (true) {
      std::string question = fmt::format(
          "For which instance group would you like to run {}? ", subcmd);
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
      instance_group_result =
          instance_manager_.SelectGroup(selector_args, envs, extra_queries);
      if (instance_group_result.ok()) {
        break;
      }
      std::string cannot_find_group_name = fmt::format(
          "\n  Failed to find a group whose name is {}\"{}\"{}\n\n",
          TerminalColor(is_tty, TerminalColors::kBoldRed), chosen_group_name,
          TerminalColor(is_tty, TerminalColors::kReset));
      CF_EXPECT_EQ(WriteAll(request.Err(), cannot_find_group_name),
                   (ssize_t)cannot_find_group_name.size());
    }
  }

  auto& instance_group = *instance_group_result;
  auto android_host_out = instance_group.HostArtifactsPath();
  auto home = instance_group.HomeDir();
  auto bin = CF_EXPECT(GetBin(subcmd, android_host_out));
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
  extracted_info.invocation_info = result;
  extracted_info.group = instance_group;
  return extracted_info;
}

Result<std::string> CvdGenericCommandHandler::GetBin(
    const std::string& subcmd) const {
  CF_EXPECT(Contains(command_to_binary_map_, subcmd));
  const auto& bin_name = command_to_binary_map_.at(subcmd);
  return bin_name;
}

Result<std::string> CvdGenericCommandHandler::GetBin(
    const std::string& subcmd, const std::string& host_artifacts_path) const {
  CF_EXPECT(Contains(command_to_binary_map_, subcmd));
  return GetBin(subcmd);
}

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdGenericCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdGenericCommandHandler(instance_manager));
}

}  // namespace cuttlefish
