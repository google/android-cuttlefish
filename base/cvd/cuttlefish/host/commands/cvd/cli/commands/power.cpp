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

#include "host/commands/cvd/cli/commands/power.h"

#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/cli/commands/host_tool_target.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/flag.h"
#include "host/commands/cvd/cli/selector/selector.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Trigger power button event on the device, reset device to first boot "
    "state, restart device";

class CvdDevicePowerCommandHandler : public CvdServerHandler {
 public:
  CvdDevicePowerCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {
    cvd_power_operations_["restart"] =
        [this](const std::string& android_host_out) -> Result<std::string> {
      return CF_EXPECT(RestartDeviceBin(android_host_out));
    };
    cvd_power_operations_["powerwash"] =
        [this](const std::string& android_host_out) -> Result<std::string> {
      return CF_EXPECT(PowerwashBin(android_host_out));
    };
    cvd_power_operations_["powerbtn"] =
        [this](const std::string& android_host_out) -> Result<std::string> {
      return CF_EXPECT(PowerbtnBin(android_host_out));
    };
  }

  Result<bool> CanHandle(const CommandRequest& request) const override {
    return Contains(cvd_power_operations_, request.Subcommand());
  }

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));
    const cvd_common::Envs& env = request.Env();

    std::string op = request.Subcommand();
    std::vector<std::string> subcmd_args = request.SubcommandArguments();
    bool is_help = CF_EXPECT(IsHelp(subcmd_args));

    // may modify subcmd_args by consuming in parsing
    Command command =
        is_help ? CF_EXPECT(HelpCommand(request, op, subcmd_args, env))
                : CF_EXPECT(NonHelpCommand(request, op, subcmd_args, env));

    siginfo_t infop;
    command.Start().Wait(&infop, WEXITED);

    CF_EXPECT(CheckProcessExitedNormally(infop));
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

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return false; }

  Result<std::string> DetailedHelp(
      std::vector<std::string>& arguments) const override {
    static constexpr char kDetailedHelpText[] =
        "Run cvd {} --help for full help text";
    std::string replacement = "<command>";
    if (!arguments.empty()) {
      replacement = arguments.front();
    }
    return fmt::format(kDetailedHelpText, replacement);
  }

 private:
  Result<std::string> RestartDeviceBin(
      const std::string& android_host_out) const {
    return CF_EXPECT(HostToolTarget(android_host_out).GetRestartBinName());
  }

  Result<std::string> PowerwashBin(const std::string& android_host_out) const {
    return CF_EXPECT(HostToolTarget(android_host_out).GetPowerwashBinName());
  }

  Result<std::string> PowerbtnBin(const std::string& android_host_out) const {
    return CF_EXPECT(HostToolTarget(android_host_out).GetPowerBtnBinName());
  }

  Result<Command> HelpCommand(const CommandRequest& request,
                              const std::string& op,
                              const cvd_common::Args& subcmd_args,
                              cvd_common::Envs envs) {
    auto android_host_out = CF_EXPECT(AndroidHostPath(envs));
    const auto bin_base = CF_EXPECT(GetBin(op, android_host_out));
    auto cvd_power_bin_path =
        ConcatToString(android_host_out, "/bin/", bin_base);
    std::string home = Contains(envs, "HOME") ? envs.at("HOME")
                                              : CF_EXPECT(SystemWideUserHome());
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;
    ConstructCommandParam construct_cmd_param{.bin_path = cvd_power_bin_path,
                                              .home = home,
                                              .args = subcmd_args,
                                              .envs = std::move(envs),
                                              .working_dir = CurrentDirectory(),
                                              .command_name = bin_base};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<Command> NonHelpCommand(const CommandRequest& request,
                                 const std::string& op,
                                 cvd_common::Args& subcmd_args,
                                 cvd_common::Envs envs) {
    // test if there is --instance_num flag
    CvdFlag<std::int32_t> instance_num_flag("instance_num");
    auto instance_num_opt =
        CF_EXPECT(instance_num_flag.FilterFlag(subcmd_args));
    auto [instance, group] =
        instance_num_opt.has_value()
            ? CF_EXPECT(instance_manager_.FindInstanceWithGroup(
                  {.instance_id = *instance_num_opt}))
            : CF_EXPECT(selector::SelectInstance(instance_manager_, request));
    const auto& home = group.Proto().home_directory();

    const auto& android_host_out = group.Proto().host_artifacts_path();
    const auto bin_base = CF_EXPECT(GetBin(op, android_host_out));
    auto cvd_power_bin_path =
        ConcatToString(android_host_out, "/bin/", bin_base);

    cvd_common::Args cvd_env_args{subcmd_args};
    cvd_env_args.push_back(ConcatToString("--instance_num=", instance.id()));
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
    std::cerr << command_to_issue.str();

    ConstructCommandParam construct_cmd_param{.bin_path = cvd_power_bin_path,
                                              .home = home,
                                              .args = cvd_env_args,
                                              .envs = std::move(envs),
                                              .working_dir = CurrentDirectory(),
                                              .command_name = bin_base};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<bool> IsHelp(const cvd_common::Args& cmd_args) const {
    if (cmd_args.empty()) {
      return false;
    }
    // cvd restart/powerwash/powerbtn --help, --helpxml, etc or simply cvd
    // restart
    if (CF_EXPECT(IsHelpSubcmd(cmd_args))) {
      return true;
    }
    // cvd restart/powerwash/powerbtn help <subcommand> format
    return (cmd_args.front() == "help");
  }

  Result<std::string> GetBin(const std::string& subcmd,
                             const std::string& android_host_out) const {
    CF_EXPECT(Contains(cvd_power_operations_, subcmd),
              subcmd << " is not supported.");
    return CF_EXPECT((cvd_power_operations_.at(subcmd))(android_host_out));
  }

  InstanceManager& instance_manager_;
  using BinGetter = std::function<Result<std::string>(const std::string&)>;
  std::unordered_map<std::string, BinGetter> cvd_power_operations_;
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdDevicePowerCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdDevicePowerCommandHandler(instance_manager));
}

}  // namespace cuttlefish
