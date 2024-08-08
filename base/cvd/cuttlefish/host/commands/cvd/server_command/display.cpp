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

#include "host/commands/cvd/server_command/display.h"

#include <android-base/strings.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {
constexpr char kSummaryHelpText[] =
    R"(Enables hotplug/unplug of displays from running cuttlefish virtual devices)";

constexpr char kDetailedHelpText[] =
    R"(

usage: cvd display <command> <args>

Commands:
    help <command>      Print help for a command.
    add                 Adds a new display to a given device.
    list                Prints the currently connected displays.
    remove              Removes a display from a given device.
)";

class CvdDisplayCommandHandler : public CvdServerHandler {
 public:
  CvdDisplayCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager},
        cvd_display_operations_{"display"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return Contains(cvd_display_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto [_, subcmd_args] = ParseInvocation(request.Message());

    bool is_help = CF_EXPECT(IsHelp(subcmd_args));
    // may modify subcmd_args by consuming in parsing
    Command command =
        is_help ? CF_EXPECT(HelpCommand(request, subcmd_args, envs))
                : CF_EXPECT(NonHelpCommand(request, subcmd_args, envs));

    siginfo_t infop;
    command.Start().Wait(&infop, WEXITED);

    return ResponseFromSiginfo(infop);
  }

  cvd_common::Args CmdList() const override {
    return cvd_common::Args(cvd_display_operations_.begin(),
                            cvd_display_operations_.end());
  }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<Command> HelpCommand(const RequestWithStdio& request,
                              const cvd_common::Args& subcmd_args,
                              cvd_common::Envs envs) {
    CF_EXPECT(Contains(envs, kAndroidHostOut));
    auto cvd_display_bin_path =
        ConcatToString(envs.at(kAndroidHostOut), "/bin/", kDisplayBin);
    std::string home = Contains(envs, "HOME")
                           ? envs.at("HOME")
                           : CF_EXPECT(SystemWideUserHome());
    envs["HOME"] = home;
    envs[kAndroidSoongHostOut] = envs.at(kAndroidHostOut);
    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_display_bin_path,
        .home = home,
        .args = subcmd_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = kDisplayBin};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<Command> NonHelpCommand(const RequestWithStdio& request,
                                 cvd_common::Args& subcmd_args,
                                 cvd_common::Envs envs) {
    // test if there is --instance_num flag
    CvdFlag<std::int32_t> instance_num_flag("instance_num");
    auto instance_num_opt =
        CF_EXPECT(instance_num_flag.FilterFlag(subcmd_args));
    selector::Queries extra_queries;
    if (instance_num_opt) {
      extra_queries.emplace_back(selector::kInstanceIdField, *instance_num_opt);
    }

    const auto& selector_opts =
        request.Message().command_request().selector_opts();
    const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());

    auto [instance, group] = CF_EXPECT(
        instance_manager_.SelectInstance(selector_args, envs, extra_queries));
    const auto& home = group.Proto().home_directory();

    const auto& android_host_out = group.Proto().host_artifacts_path();
    auto cvd_display_bin_path =
        ConcatToString(android_host_out, "/bin/", kDisplayBin);

    cvd_common::Args cvd_env_args{subcmd_args};
    cvd_env_args.push_back(
        ConcatToString("--instance_num=", instance.id()));
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;

    std::cerr << "HOME=" << home << " " << kAndroidHostOut << "="
              << android_host_out << " " << kAndroidSoongHostOut << "="
              << android_host_out << " " << cvd_display_bin_path << " ";
    for (const auto& arg : cvd_env_args) {
      std::cerr << arg << " ";
    }

    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_display_bin_path,
        .home = home,
        .args = cvd_env_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = kDisplayBin};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<bool> IsHelp(const cvd_common::Args& cmd_args) const {
    // cvd display --help, --helpxml, etc or simply cvd display
    if (cmd_args.empty() || CF_EXPECT(IsHelpSubcmd(cmd_args))) {
      return true;
    }
    // cvd display help <subcommand> format
    return (cmd_args.front() == "help");
  }

  InstanceManager& instance_manager_;
  std::vector<std::string> cvd_display_operations_;
  static constexpr char kDisplayBin[] = "cvd_internal_display";
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdDisplayCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdDisplayCommandHandler(instance_manager));
}

}  // namespace cuttlefish
