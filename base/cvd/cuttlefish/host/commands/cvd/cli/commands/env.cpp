/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/env.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kCvdEnvBin[] = "cvd_internal_env";

constexpr char kSummaryHelpText[] =
    R"(Enumerate + Query APIs for all gRPC services made available by this virtual device instance)";

constexpr char kDetailedHelpText[] = R"(
Usage:
cvd env ls - lists all available services per instance
cvd env ls $SERVICE_NAME - lists all methods for $SERVICE_NAME
cvd env ls $SERVICE_NAME $METHOD_NAME - list information on input + output message types for $SERVICE_NAME#$METHOD_NAME
cvd env type $SERVICE_NAME $REQUEST_MESSAGE_TYPE - outputs the proto the specified request message type
)";

Result<Command> HelpCommand(const CommandRequest& request) {
  cvd_common::Envs envs_copy = request.Env();
  std::vector<std::string> help_args = request.SubcommandArguments();
  if (help_args.empty()) {
    help_args.push_back("--help");
  }
  envs_copy[kAndroidHostOut] = CF_EXPECT(AndroidHostPath(request.Env()));
  return CF_EXPECT(
      ConstructCvdHelpCommand(kCvdEnvBin, envs_copy, help_args, request));
}

Result<Command> NonHelpCommand(InstanceManager& instance_manager,
                               const CommandRequest& request) {
  auto [instance, group] =
      CF_EXPECT(selector::SelectInstance(instance_manager, request));
  const auto& home = group.Proto().home_directory();

  const std::string& android_host_out = group.Proto().host_artifacts_path();
  const std::string cvd_env_bin_path =
      absl::StrCat(android_host_out, "/bin/", kCvdEnvBin);
  const std::string internal_device_name = absl::StrCat("cvd-", instance.id());

  const cvd_common::Args& subcmd_args = request.SubcommandArguments();
  cvd_common::Args cvd_env_args{internal_device_name};
  cvd_env_args.insert(cvd_env_args.end(), subcmd_args.begin(),
                      subcmd_args.end());

  return CF_EXPECT(
      ConstructCvdGenericNonHelpCommand({.bin_file = kCvdEnvBin,
                                         .envs = request.Env(),
                                         .cmd_args = cvd_env_args,
                                         .android_host_out = android_host_out,
                                         .home = home,
                                         .verbose = true},
                                        request));
}

}  // namespace

CvdEnvCommandHandler::CvdEnvCommandHandler(InstanceManager& instance_manager)
    : instance_manager_{instance_manager} {}

Result<void> CvdEnvCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> subcmd_args = request.SubcommandArguments();

  // --help and cvd env help are intercepted
  bool is_help = request.SubcommandArguments().empty();

  Command command = is_help
                        ? CF_EXPECT(HelpCommand(request))
                        : CF_EXPECT(NonHelpCommand(instance_manager_, request));

  siginfo_t infop;  // NOLINT(misc-include-cleaner)
  command.Start().Wait(&infop, WEXITED);

  CF_EXPECT(CheckProcessExitedNormally(infop));
  return {};
}

cvd_common::Args CvdEnvCommandHandler::CmdList() const { return {"env"}; }

std::string CvdEnvCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdEnvCommandHandler::RequiresDeviceExists() const { return true; }

Result<std::string> CvdEnvCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  Result<Command> help_cmd_res = HelpCommand(request);
  if (!help_cmd_res.ok()) {
    // Couldn't find an underlying binary to defer to for help
    return kDetailedHelpText;
  }
  std::string stdout;
  int res = RunWithManagedStdio(std::move(help_cmd_res.value()), nullptr,
                                &stdout, nullptr);
  // gflags returns exit code 1 when --help is given
  CF_EXPECTF(res == 0 || res == 1,
             "Failed to execute internal env binary, exit code: {}", res);
  return stdout;
}

std::unique_ptr<CvdCommandHandler> NewCvdEnvCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdEnvCommandHandler(instance_manager));
}
}  // namespace cuttlefish
