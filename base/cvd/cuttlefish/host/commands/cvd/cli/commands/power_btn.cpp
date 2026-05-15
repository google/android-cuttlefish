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

#include "cuttlefish/host/commands/cvd/cli/commands/power_btn.h"

#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Trigger power button event on the device";

constexpr char kPowerBtnCmd[] = "powerbtn";

}  // namespace

CvdDevicePowerBtnCommandHandler::CvdDevicePowerBtnCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_{instance_manager} {}

Result<void> CvdDevicePowerBtnCommandHandler::Handle(
    const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags({UnexpectedArgumentGuard()}, args));
  auto [instance, _] =
      CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                "Unable to select an instance");
  CF_EXPECT(instance.PressPowerBtn());
  return {};
}

cvd_common::Args CvdDevicePowerBtnCommandHandler::CmdList() const {
  return {kPowerBtnCmd};
}

std::string CvdDevicePowerBtnCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdDevicePowerBtnCommandHandler::RequiresDeviceExists() const {
  return true;
}

Result<std::string> CvdDevicePowerBtnCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  return kSummaryHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdDevicePowerBtnCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdDevicePowerBtnCommandHandler(instance_manager));
}

}  // namespace cuttlefish
