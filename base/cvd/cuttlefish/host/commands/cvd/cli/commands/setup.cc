/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/setup.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_join.h"

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/libs/vm_manager/host_configuration.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Configure the host for Cuttlefish";

constexpr char kDetailedHelpText[] =
    R"(cvd setup - configure host for cuttlefish

Checks host configuration (kernel version, group memberships) and automatically applies fixes (e.g., adding user to kvm and cvdnetwork groups). Some fixes may require sudo permissions.

Usage:
  cvd setup
)";

using vm_manager::HostConfigurationAction;
using vm_manager::ValidateHostConfiguration;

}  // namespace

Result<void> CvdSetupHandler::Handle(const CommandRequest& request) {
  std::vector<HostConfigurationAction> actions =
      CF_EXPECT(ValidateHostConfiguration());
  if (actions.empty()) {
    std::cout << "Host configuration is already valid. No setup required."
              << std::endl;
    return {};
  }

  std::cout << "Applying host configuration fixes..." << std::endl;
  for (const HostConfigurationAction& action : actions) {
    if (!action.description.empty()) {
      std::cout << "Purpose: " << action.description << std::endl;
    }
    if (action.command.empty()) {
      std::cout
          << "Manual intervention required (no automated command available)."
          << std::endl;
      continue;
    }

    std::cout << "Running: " << absl::StrJoin(action.command, " ") << std::endl;
    const int status = Execute(action.command);
    CF_EXPECTF(status == 0, "Failed to execute command: `{}`, exit code: {}",
               absl::StrJoin(action.command, " "), status);
  }

  std::cout << "Setup completed successfully." << std::endl;
  return {};
}

cvd_common::Args CvdSetupHandler::CmdList() const { return {"setup"}; }

std::string CvdSetupHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdSetupHandler::RequiresDeviceExists() const { return false; }

bool CvdSetupHandler::RequiresHostConfiguration() const {
  return false;
}

Result<std::string> CvdSetupHandler::DetailedHelp(const CommandRequest&) {
  return kDetailedHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdSetupHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdSetupHandler());
}

}  // namespace cuttlefish
