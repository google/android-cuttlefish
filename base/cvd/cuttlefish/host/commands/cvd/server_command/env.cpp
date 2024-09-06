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

#include "host/commands/cvd/server_command/env.h"

#include <android-base/strings.h>

#include <string>
#include <vector>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Enumerate + Query APIs for all gRPC services made available by this virtual device instance)";

constexpr char kDetailedHelpText[] = R"(
Usage:
cvd env ls - lists all available services per instance
cvd env ls $SERVICE_NAME - lists all methods for $SERVICE_NAME
cvd env ls $SERVICE_NAME $METHOD_NAME - list information on input + output message types for $SERVICE_NAME#$METHOD_NAME
cvd env type $SERVICE_NAME $REQUEST_MESSAGE_TYPE - outputs the proto the specified request message type
)";

class CvdEnvCommandHandler : public CvdServerHandler {
 public:
  CvdEnvCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager}, cvd_env_operations_{"env"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return Contains(cvd_env_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd_common::Envs envs = request.Envs();

    auto [_, subcmd_args] = ParseInvocation(request.Message());

    /*
     * cvd_env --help only. Not --helpxml, etc.
     *
     * Otherwise, IsHelpSubcmd() should be used here instead.
     */
    auto help_flag = CvdFlag("help", false);
    cvd_common::Args subcmd_args_copy{subcmd_args};
    auto help_parse_result = help_flag.CalculateFlag(subcmd_args_copy);
    bool is_help = help_parse_result.ok() && (*help_parse_result);

    Command command =
        is_help ? CF_EXPECT(HelpCommand(request, subcmd_args, envs))
                : CF_EXPECT(NonHelpCommand(request, subcmd_args, envs));

    siginfo_t infop;
    command.Start().Wait(&infop, WEXITED);

    return ResponseFromSiginfo(infop);
  }

  cvd_common::Args CmdList() const override {
    return cvd_common::Args(cvd_env_operations_.begin(),
                            cvd_env_operations_.end());
  }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<Command> HelpCommand(const RequestWithStdio& request,
                              const cvd_common::Args& subcmd_args,
                              const cvd_common::Envs& envs) {
    cvd_common::Envs envs_copy = envs;
    envs_copy[kAndroidHostOut] = CF_EXPECT(AndroidHostPath(envs));
    return CF_EXPECT(
        ConstructCvdHelpCommand(kCvdEnvBin, envs_copy, subcmd_args, request));
  }

  Result<Command> NonHelpCommand(const RequestWithStdio& request,
                                 const cvd_common::Args& subcmd_args,
                                 const cvd_common::Envs& envs) {
    const auto selector_args = request.SelectorArgs();

    auto [instance, group] =
        CF_EXPECT(instance_manager_.SelectInstance(selector_args, envs));
    const auto& home = group.Proto().home_directory();

    const auto& android_host_out = group.Proto().host_artifacts_path();
    auto cvd_env_bin_path =
        ConcatToString(android_host_out, "/bin/", kCvdEnvBin);
    const auto& internal_device_name = fmt::format("cvd-{}", instance.id());

    cvd_common::Args cvd_env_args{internal_device_name};
    cvd_env_args.insert(cvd_env_args.end(), subcmd_args.begin(),
                        subcmd_args.end());

    return CF_EXPECT(
        ConstructCvdGenericNonHelpCommand({.bin_file = kCvdEnvBin,
                                           .envs = envs,
                                           .cmd_args = cvd_env_args,
                                           .android_host_out = android_host_out,
                                           .home = home,
                                           .verbose = true},
                                          request));
  }

  InstanceManager& instance_manager_;
  std::vector<std::string> cvd_env_operations_;

  static constexpr char kCvdEnvBin[] = "cvd_internal_env";
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdEnvCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdEnvCommandHandler(instance_manager));
}
}  // namespace cuttlefish
