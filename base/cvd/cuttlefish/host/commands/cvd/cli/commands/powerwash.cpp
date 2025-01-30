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

#include "host/commands/cvd/cli/commands/powerwash.h"

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/cli/selector/selector.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/instances/instance_manager.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Reset device to first boot state";
constexpr char kDetailedHelpText[] =
    R"(powerwash: Resets device state to first boot. Functionaly equivalent to
removing the device and creating it again, but more efficient.

Flags:
    -boot_timeout (How many seconds to wait for the device to reboot.)
      type: int32 default: 500
    -wait_for_launcher (How many seconds to wait for the launcher to respond to
      the status command. A value of zero means wait indefinitely.) type: int32
      default: 30
)";

constexpr char kPowerwashCmd[] = "powerwash";

struct PowerwashOptions {
  int wait_for_launcher_seconds = 30;
  int boot_timeout_seconds = 500;

  std::vector<Flag> Flags() {
    return {
        GflagsCompatFlag("wait_for_launcher", wait_for_launcher_seconds),
        GflagsCompatFlag("boot_timeout", boot_timeout_seconds),
    };
  }
};

class CvdDevicePowerwashCommandHandler : public CvdCommandHandler {
 public:
  CvdDevicePowerwashCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    std::vector<std::string> subcmd_args = request.SubcommandArguments();

    // TODO: chadreynolds - check if this can be removed
    if (CF_EXPECT(HasHelpFlag(subcmd_args))) {
      std::cout << kDetailedHelpText << std::endl;
      return {};
    }

    auto [instance, unused] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");

    PowerwashOptions options;
    CF_EXPECT(ConsumeFlags(options.Flags(), subcmd_args));

    CF_EXPECT(instance.PowerWash(
        std::chrono::seconds(options.wait_for_launcher_seconds),
        std::chrono::seconds(options.boot_timeout_seconds)));
    return {};
  }

  cvd_common::Args CmdList() const override { return {kPowerwashCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdDevicePowerwashCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdDevicePowerwashCommandHandler(instance_manager));
}

}  // namespace cuttlefish
