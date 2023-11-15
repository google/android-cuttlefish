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

#include "host/commands/cvd/server_command/power.h"

#include <android-base/strings.h>

#include <functional>
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

class CvdDevicePowerCommandHandler : public CvdServerHandler {
 public:
  CvdDevicePowerCommandHandler(HostToolTargetManager& host_tool_target_manager,
                               InstanceManager& instance_manager,
                               SubprocessWaiter& subprocess_waiter)
      : host_tool_target_manager_(host_tool_target_manager),
        instance_manager_{instance_manager},
        subprocess_waiter_(subprocess_waiter) {
    cvd_power_operations_["restart"] =
        [this](const std::string& android_host_out) -> Result<std::string> {
      return CF_EXPECT(RestartDeviceBin(android_host_out));
    };
    cvd_power_operations_["powerwash"] =
        [this](const std::string& android_host_out) -> Result<std::string> {
      return CF_EXPECT(PowerwashBin(android_host_out));
    };
  }

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return Contains(cvd_power_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    const uid_t uid = request.Credentials()->uid;
    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto [op, subcmd_args] = ParseInvocation(request.Message());
    bool is_help = IsHelp(subcmd_args);

    // may modify subcmd_args by consuming in parsing
    Command command =
        is_help
            ? CF_EXPECT(HelpCommand(request, uid, op, subcmd_args, envs))
            : CF_EXPECT(NonHelpCommand(request, uid, op, subcmd_args, envs));
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
    cvd_common::Args valid_ops;
    valid_ops.reserve(cvd_power_operations_.size());
    for (const auto& [op, _] : cvd_power_operations_) {
      valid_ops.push_back(op);
    }
    return valid_ops;
  }

 private:
  Result<std::string> RestartDeviceBin(
      const std::string& android_host_out) const {
    auto restart_bin = CF_EXPECT(host_tool_target_manager_.ExecBaseName({
        .artifacts_path = android_host_out,
        .op = "restart",
    }));
    return restart_bin;
  }

  Result<std::string> PowerwashBin(const std::string& android_host_out) const {
    auto powerwash_bin = CF_EXPECT(host_tool_target_manager_.ExecBaseName({
        .artifacts_path = android_host_out,
        .op = "powerwash",
    }));
    return powerwash_bin;
  }

  Result<Command> HelpCommand(const RequestWithStdio& request, const uid_t uid,
                              const std::string& op,
                              const cvd_common::Args& subcmd_args,
                              cvd_common::Envs envs) {
    CF_EXPECT(Contains(envs, kAndroidHostOut));
    const auto bin_base = CF_EXPECT(GetBin(op, envs.at(kAndroidHostOut)));
    auto cvd_power_bin_path =
        ConcatToString(envs.at(kAndroidHostOut), "/bin/", bin_base);
    std::string home = Contains(envs, "HOME")
                           ? envs.at("HOME")
                           : CF_EXPECT(SystemWideUserHome(uid));
    envs["HOME"] = home;
    envs[kAndroidSoongHostOut] = envs.at(kAndroidHostOut);
    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_power_bin_path,
        .home = home,
        .args = subcmd_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = bin_base,
        .in = request.In(),
        .out = request.Out(),
        .err = request.Err()};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<Command> NonHelpCommand(const RequestWithStdio& request,
                                 const uid_t uid, const std::string& op,
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

    auto instance = CF_EXPECT(instance_manager_.SelectInstance(
        selector_args, extra_queries, envs, uid));
    const auto& instance_group = instance.ParentGroup();
    const auto& home = instance_group.HomeDir();

    const auto& android_host_out = instance_group.HostArtifactsPath();
    const auto bin_base = CF_EXPECT(GetBin(op, android_host_out));
    auto cvd_power_bin_path =
        ConcatToString(android_host_out, "/bin/", bin_base);

    cvd_common::Args cvd_env_args{subcmd_args};
    cvd_env_args.push_back(
        ConcatToString("--instance_num=", instance.InstanceId()));
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;

    std::stringstream command_to_issue;
    command_to_issue << "HOME=" << home << " " << kAndroidHostOut << "="
                     << android_host_out << " " << kAndroidSoongHostOut << "="
                     << android_host_out << " " << cvd_power_bin_path << " ";
    for (const auto& arg : cvd_env_args) {
      command_to_issue << arg << " ";
    }
    WriteAll(request.Err(), command_to_issue.str());

    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_power_bin_path,
        .home = home,
        .args = cvd_env_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = bin_base,
        .in = request.In(),
        .out = request.Out(),
        .err = request.Err()};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  bool IsHelp(const cvd_common::Args& cmd_args) const {
    if (cmd_args.empty()) {
      return false;
    }
    // cvd restart/powerwash --help, --helpxml, etc or simply cvd restart
    if (IsHelpSubcmd(cmd_args)) {
      return true;
    }
    // cvd restart/powerwash help <subcommand> format
    return (cmd_args.front() == "help");
  }

  Result<std::string> GetBin(const std::string& subcmd,
                             const std::string& android_host_out) const {
    CF_EXPECT(Contains(cvd_power_operations_, subcmd),
              subcmd << " is not supported.");
    return CF_EXPECT((cvd_power_operations_.at(subcmd))(android_host_out));
  }

  HostToolTargetManager& host_tool_target_manager_;
  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  using BinGetter = std::function<Result<std::string>(const std::string&)>;
  std::unordered_map<std::string, BinGetter> cvd_power_operations_;
};

std::unique_ptr<CvdServerHandler> NewCvdDevicePowerCommandHandler(
    HostToolTargetManager& host_tool_target_manager,
    InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter) {
  return std::unique_ptr<CvdServerHandler>(new CvdDevicePowerCommandHandler(
      host_tool_target_manager, instance_manager, subprocess_waiter));
}

}  // namespace cuttlefish
