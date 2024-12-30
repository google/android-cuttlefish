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

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/cli/flag.h"
#include "host/commands/cvd/cli/selector/selector.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/instance_manager.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Trigger power button event on the device, reset device to first boot "
    "state, restart device";

constexpr char kRestartCmd[] = "restart";
constexpr char kPowerBtnCmd[] = "powerbtn";
constexpr char kPowerwashCmd[] = "powerwash";

struct RestartOptions {
  int wait_for_launcher_seconds = 30;
  int boot_timeout_seconds = 500;
};
Result<RestartOptions> RestartOptionsFromArgs(cvd_common::Args& args) {
  RestartOptions ret;

  CvdFlag<std::int32_t> wait_for_launcher_flag("wait_for_launcher");
  auto wait_for_launcher_opt =
      CF_EXPECT(wait_for_launcher_flag.FilterFlag(args));
  if (wait_for_launcher_opt.has_value()) {
    ret.wait_for_launcher_seconds = wait_for_launcher_opt.value();
  }

  CvdFlag<std::int32_t> boot_timeout_flag("wait_for_launcher");
  auto boot_timeout_opt = CF_EXPECT(boot_timeout_flag.FilterFlag(args));
  if (boot_timeout_opt.has_value()) {
    ret.boot_timeout_seconds = boot_timeout_opt.value();
  }

  return ret;
}

class CvdDevicePowerCommandHandler : public CvdCommandHandler {
 public:
  CvdDevicePowerCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {
    help_string_by_op_[kRestartCmd] =
        R"(restart: Reboots the virtual device

Flags:
    -boot_timeout (How many seconds to wait for the device to reboot.)
      type: int32 default: 500
    -wait_for_launcher (How many seconds to wait for the launcher to respond to
      the status command. A value of zero means wait indefinitely.) type: int32
      default: 30
)";

    help_string_by_op_[kPowerwashCmd] =
        R"(powerwash: Resets device state to first boot. Functionaly equivalent to
removing the device and creating it again, but more efficient.

Flags:
    -boot_timeout (How many seconds to wait for the device to reboot.)
      type: int32 default: 500
    -wait_for_launcher (How many seconds to wait for the launcher to respond to
      the status command. A value of zero means wait indefinitely.) type: int32
      default: 30
)";

    help_string_by_op_[kPowerBtnCmd] =
        "powerbtn: Triggers a power button event\n";
  }

  Result<bool> CanHandle(const CommandRequest& request) const override {
    auto subcmd = request.Subcommand();
    return subcmd == kRestartCmd || subcmd == kPowerwashCmd ||
           subcmd == kPowerBtnCmd;
  }

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    std::vector<std::string> subcmd_args = request.SubcommandArguments();

    std::string op = request.Subcommand();
    if (CF_EXPECT(IsHelp(subcmd_args))) {
      std::cout << help_string_by_op_[op];
      return {};
    }

    auto [instance, group] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");
    if (op == kRestartCmd) {
      RestartOptions options = CF_EXPECT(RestartOptionsFromArgs(subcmd_args));
      CF_EXPECT(instance.Restart(
          std::chrono::seconds(options.wait_for_launcher_seconds),
          std::chrono::seconds(options.boot_timeout_seconds)));
    } else if (op == kPowerwashCmd) {
      RestartOptions options = CF_EXPECT(RestartOptionsFromArgs(subcmd_args));
      CF_EXPECT(instance.PowerWash(
          std::chrono::seconds(options.wait_for_launcher_seconds),
          std::chrono::seconds(options.boot_timeout_seconds)));
    } else if (op == kPowerBtnCmd) {
      CF_EXPECT(instance.PressPowerBtn());
    } else {
      LOG(FATAL) << "Unsupported subcommand: " << op;
    }
    return {};
  }

  cvd_common::Args CmdList() const override {
    return {kRestartCmd, kPowerwashCmd, kPowerBtnCmd};
  }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return false; }

  Result<std::string> DetailedHelp(
      std::vector<std::string>& arguments) const override {
    CHECK(!arguments.empty()) << "Missing subcommand";
    return help_string_by_op_.at(arguments.front());
  }

 private:
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

  InstanceManager& instance_manager_;
  std::unordered_map<std::string, std::string> help_string_by_op_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdDevicePowerCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdDevicePowerCommandHandler(instance_manager));
}

}  // namespace cuttlefish
