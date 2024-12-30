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

#include "host/commands/cvd/cli/commands/restart.h"

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

constexpr char kSummaryHelpText[] = "Restart device";
constexpr char kDetailedHelpText[] =
    R"(restart: Reboots the virtual device

Flags:
    -boot_timeout (How many seconds to wait for the device to reboot.)
      type: int32 default: 1000
    -wait_for_launcher (How many seconds to wait for the launcher to respond to
      the status command. A value of zero means wait indefinitely.) type: int32
      default: 30
)";

constexpr char kRestartCmd[] = "restart";

struct RestartOptions {
  int wait_for_launcher_seconds = 30;
  int boot_timeout_seconds = 500;
};
Result<RestartOptions> OptionsFromArgs(cvd_common::Args& args) {
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

class CvdDeviceRestartCommandHandler : public CvdCommandHandler {
 public:
  CvdDeviceRestartCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    std::vector<std::string> subcmd_args = request.SubcommandArguments();

    if (CF_EXPECT(IsHelpSubcmd(subcmd_args))) {
      std::cout << kDetailedHelpText << std::endl;
      return {};
    }

    auto [instance, _] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");
    RestartOptions options = CF_EXPECT(OptionsFromArgs(subcmd_args));
    CF_EXPECT(instance.Restart(
        std::chrono::seconds(options.wait_for_launcher_seconds),
        std::chrono::seconds(options.boot_timeout_seconds)));
    return {};
  }

  cvd_common::Args CmdList() const override { return {kRestartCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdDeviceRestartCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdDeviceRestartCommandHandler(instance_manager));
}

}  // namespace cuttlefish
