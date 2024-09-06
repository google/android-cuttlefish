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

#include "host/commands/cvd/server_command/bugreport.h"

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
#include "host/commands/cvd/interruptible_terminal.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Run cvd bugreport --help for command description";

class CvdBugreportCommandHandler : public CvdServerHandler {
 public:
  CvdBugreportCommandHandler(InstanceManager& instance_manager);

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  cvd_common::Args CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  InstanceManager& instance_manager_;
  using BinGeneratorType = std::function<Result<std::string>(
      const std::string& host_artifacts_path)>;
  std::set<std::string> commands_;
  std::unique_ptr<InterruptibleTerminal> terminal_ = nullptr;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
};

CvdBugreportCommandHandler::CvdBugreportCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager),
      commands_{{"bugreport", "host_bugreport", "cvd_host_bugreport"}} {}

Result<bool> CvdBugreportCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(commands_, invocation.command);
}

Result<cvd::Response> CvdBugreportCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));

  cvd::Response response;
  response.mutable_command_response();

  auto [subcmd, cmd_args] = ParseInvocation(request.Message());
  cvd_common::Envs envs = request.Envs();

  std::string android_host_out;
  std::string home = CF_EXPECT(SystemWideUserHome());
  if (!CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    auto instance_group = CF_EXPECT(SelectGroup(instance_manager_, request));
    android_host_out = instance_group.HostArtifactsPath();
    home = instance_group.HomeDir();
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
  } else {
    android_host_out = CF_EXPECT(AndroidHostPath(envs));
  }
  auto bin_path = ConcatToString(android_host_out, "/bin/", kHostBugreportBin);

  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = home,
      .args = cmd_args,
      .envs = envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = kHostBugreportBin,
      .null_stdio = request.IsNullIo()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  siginfo_t infop;
  command.Start().Wait(&infop, WEXITED);

  return ResponseFromSiginfo(infop);
}

std::vector<std::string> CvdBugreportCommandHandler::CmdList() const {
  std::vector<std::string> subcmd_list;
  subcmd_list.reserve(commands_.size());
  for (const auto& cmd : commands_) {
    subcmd_list.emplace_back(cmd);
  }
  return subcmd_list;
}

Result<std::string> CvdBugreportCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdBugreportCommandHandler::ShouldInterceptHelp() const { return false; }

Result<std::string> CvdBugreportCommandHandler::DetailedHelp(
    std::vector<std::string>& arguments) const {
  static constexpr char kDetailedHelpText[] =
      "Run cvd {} --help for full help text";
  std::string replacement = "<command>";
  if (!arguments.empty()) {
    replacement = arguments.front();
  }
  return fmt::format(kDetailedHelpText, replacement);
}

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdBugreportCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdBugreportCommandHandler(instance_manager));
}

}  // namespace cuttlefish
