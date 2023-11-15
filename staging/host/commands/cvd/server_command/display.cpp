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

#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdDisplayCommandHandler : public CvdServerHandler {
 public:
  CvdDisplayCommandHandler(InstanceManager& instance_manager,
                           SubprocessWaiter& subprocess_waiter)
      : instance_manager_{instance_manager},
        subprocess_waiter_(subprocess_waiter),
        cvd_display_operations_{"display"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return Contains(cvd_display_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    const uid_t uid = request.Credentials()->uid;
    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto [_, subcmd_args] = ParseInvocation(request.Message());

    bool is_help = IsHelp(subcmd_args);
    // may modify subcmd_args by consuming in parsing
    Command command =
        is_help ? CF_EXPECT(HelpCommand(request, uid, subcmd_args, envs))
                : CF_EXPECT(NonHelpCommand(request, uid, subcmd_args, envs));
    SubprocessOptions options;
    CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));
    interrupt_lock.unlock();

    auto infop = CF_EXPECT(subprocess_waiter_.Wait());
    return ResponseFromSiginfo(infop);
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    CF_EXPECT(subprocess_waiter_.Interrupt());
    return {};
  }

  cvd_common::Args CmdList() const override {
    return cvd_common::Args(cvd_display_operations_.begin(),
                            cvd_display_operations_.end());
  }

 private:
  Result<Command> HelpCommand(const RequestWithStdio& request, const uid_t uid,
                              const cvd_common::Args& subcmd_args,
                              cvd_common::Envs envs) {
    CF_EXPECT(Contains(envs, kAndroidHostOut));
    auto cvd_display_bin_path =
        ConcatToString(envs.at(kAndroidHostOut), "/bin/", kDisplayBin);
    std::string home = Contains(envs, "HOME")
                           ? envs.at("HOME")
                           : CF_EXPECT(SystemWideUserHome(uid));
    envs["HOME"] = home;
    envs[kAndroidSoongHostOut] = envs.at(kAndroidHostOut);
    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_display_bin_path,
        .home = home,
        .args = subcmd_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = kDisplayBin,
        .in = request.In(),
        .out = request.Out(),
        .err = request.Err()};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<Command> NonHelpCommand(const RequestWithStdio& request,
                                 const uid_t uid, cvd_common::Args& subcmd_args,
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

    auto instance = CF_EXPECT(instance_manager_.SelectInstance(
        selector_args, extra_queries, envs, uid));
    const auto& instance_group = instance.ParentGroup();
    const auto& home = instance_group.HomeDir();

    const auto& android_host_out = instance_group.HostArtifactsPath();
    auto cvd_display_bin_path =
        ConcatToString(android_host_out, "/bin/", kDisplayBin);

    cvd_common::Args cvd_env_args{subcmd_args};
    cvd_env_args.push_back(
        ConcatToString("--instance_num=", instance.InstanceId()));
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;

    std::stringstream command_to_issue;
    command_to_issue << "HOME=" << home << " " << kAndroidHostOut << "="
                     << android_host_out << " " << kAndroidSoongHostOut << "="
                     << android_host_out << " " << cvd_display_bin_path << " ";
    for (const auto& arg : cvd_env_args) {
      command_to_issue << arg << " ";
    }
    WriteAll(request.Err(), command_to_issue.str());

    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_display_bin_path,
        .home = home,
        .args = cvd_env_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = kDisplayBin,
        .in = request.In(),
        .out = request.Out(),
        .err = request.Err()};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  bool IsHelp(const cvd_common::Args& cmd_args) const {
    // cvd display --help, --helpxml, etc or simply cvd display
    if (cmd_args.empty() || IsHelpSubcmd(cmd_args)) {
      return true;
    }
    // cvd display help <subcommand> format
    return (cmd_args.front() == "help");
  }

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> cvd_display_operations_;
  static constexpr char kDisplayBin[] = "cvd_internal_display";
};

std::unique_ptr<CvdServerHandler> NewCvdDisplayCommandHandler(
    InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdDisplayCommandHandler(instance_manager, subprocess_waiter));
}

}  // namespace cuttlefish
