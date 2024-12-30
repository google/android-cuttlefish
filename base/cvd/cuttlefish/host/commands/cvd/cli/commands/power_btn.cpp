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

#include "host/commands/cvd/cli/commands/power_btn.h"

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/cli/selector/selector.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/instance_manager.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Trigger power button event on the device";

constexpr char kPowerBtnCmd[] = "powerbtn";

class CvdDevicePowerBtnCommandHandler : public CvdCommandHandler {
 public:
  CvdDevicePowerBtnCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    if (CF_EXPECT(IsHelpSubcmd(request.SubcommandArguments()))) {
      std::cout << kSummaryHelpText << std::endl;
      return {};
    }

    auto [instance, _] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");
    CF_EXPECT(instance.PressPowerBtn());
    return {};
  }

  cvd_common::Args CmdList() const override { return {kPowerBtnCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kSummaryHelpText;
  }

 private:
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdDevicePowerBtnCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdDevicePowerBtnCommandHandler(instance_manager));
}

}  // namespace cuttlefish
