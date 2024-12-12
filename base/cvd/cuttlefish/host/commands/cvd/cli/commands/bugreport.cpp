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

#include "host/commands/cvd/cli/commands/bugreport.h"

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
#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/interruptible_terminal.h"
#include "host/commands/cvd/cli/selector/selector.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Run cvd bugreport --help for command description";

class CvdBugreportCommandHandler : public CvdServerHandler {
 public:
  CvdBugreportCommandHandler(InstanceManager& instance_manager);

  Result<void> HandleVoid(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  InstanceManager& instance_manager_;
  using BinGeneratorType = std::function<Result<std::string>(
      const std::string& host_artifacts_path)>;
  std::unique_ptr<InterruptibleTerminal> terminal_ = nullptr;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
};

CvdBugreportCommandHandler::CvdBugreportCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdBugreportCommandHandler::HandleVoid(
    const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));

  std::vector<std::string> cmd_args = request.SubcommandArguments();
  cvd_common::Envs env = request.Env();

  std::string android_host_out;
  std::string home = CF_EXPECT(SystemWideUserHome());
  if (!CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    bool has_instance_groups = CF_EXPECT(instance_manager_.HasInstanceGroups());
    CF_EXPECTF(!!has_instance_groups, "{}", NoGroupMessage(request));

    auto instance_group =
        CF_EXPECT(selector::SelectGroup(instance_manager_, request));
    android_host_out = instance_group.HostArtifactsPath();
    home = instance_group.HomeDir();
    env["HOME"] = home;
    env[kAndroidHostOut] = android_host_out;
  } else {
    android_host_out = CF_EXPECT(AndroidHostPath(env));
  }
  auto bin_path = ConcatToString(android_host_out, "/bin/", kHostBugreportBin);

  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = home,
                                            .args = cmd_args,
                                            .envs = env,
                                            .working_dir = CurrentDirectory(),
                                            .command_name = kHostBugreportBin};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  siginfo_t infop;
  command.Start().Wait(&infop, WEXITED);

  CF_EXPECT(CheckProcessExitedNormally(infop));

  return {};
}

std::vector<std::string> CvdBugreportCommandHandler::CmdList() const {
  return {"bugreport", "host_bugreport", "cvd_host_bugreport"};
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
