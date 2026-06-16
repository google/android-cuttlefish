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

#include "cuttlefish/host/commands/cvd/cli/commands/reset.h"

#include <ctype.h>
#include <fmt/format.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kResetSubcmd[] = "reset";

constexpr char kSummaryHelpText[] =
    "Remove all instance groups and kills orphaned cuttlefish processes";

static bool GetUserConfirm() {
  std::cout << "Are you sure to reset all the devices, runtime files, "
            << "and the cvd server if any [y/n]? ";
  std::string user_confirm;
  std::getline(std::cin, user_confirm);
  std::transform(user_confirm.begin(), user_confirm.end(), user_confirm.begin(),
                 ::tolower);
  return (user_confirm == "y" || user_confirm == "yes");
}
}  // namespace

CvdResetCommandHandler::CvdResetCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdResetCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> subcmd_args = request.SubcommandArguments();
  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  CF_EXPECT(
      ConsumeFlags(flags, subcmd_args, {.fail_on_unexpected_argument = true}));

  // cvd reset. Give one more opportunity
  if (!flags_.is_confirmed_by_flag && !GetUserConfirm()) {
    std::cout << "For more details: " << "  cvd help reset" << std::endl;
    return {};
  }

  if (flags_.clean_runtime_dir) {
    CF_EXPECT(instance_manager_.ResetAndClearInstanceDirs());
  } else {
    CF_EXPECT(instance_manager_.Reset());
  }
  return {};
}

cvd_common::Args CvdResetCommandHandler::CmdList() const {
  return {kResetSubcmd};
}

std::string CvdResetCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

std::vector<HelpParagraph> CvdResetCommandHandler::Description() const {
  return {
      HelpParagraph::Raw(
          "Usage:\n    cvd reset [--yes] [--noclean-runtime-dir]"),
      HelpParagraph("Warning: `cvd reset` is an experimental command and "
                    "should only be used as a last resort. Prefer `cvd remove` "
                    "and/or `cvd clear` instead."),
      HelpParagraph(
          "Attempts to remove any trace of running Cuttlefish devices *owned "
          "by the current user*. This includes Cuttlefish devices not tracked "
          "by CVD, such as those started by the legacy launch_cvd invocations. "
          "It's particularly useful for file-in-use errors and when ADB "
          "remains connected to untracked devices."),
      HelpParagraph(
          "This is a destructive operation that cannot be undone, so the "
          "command always asks for confirmation from the user and exits if it "
          "can't get it. Confirmation can be provided on the command line with "
          "the --yes flag which allows the command to be used in scripts and "
          "other non-interactive use cases."),
      HelpParagraph(
          "By default, all instance directories and files are deleted as part "
          "of the reset. It is possible to skip this step for untracked "
          "instances (those not members of any known instance group) by "
          "passing the --noclean-runtime-dir flag. This allows examining the "
          "runtime files of any untracked instances after the reset. Tracked "
          "instances can be managed using other commands (like stop) to "
          "preserve their files, so this option doesn't apply to them and "
          "their runtime files are always deleted during reset."),
      HelpParagraph("`cvd reset` executes the following steps:"),
      HelpParagraph("  1. Stop and remove all known instance groups."),
      HelpParagraph("  2. Gracefully stop all remaining devices that CVD can "
                    "reach and optionally clean their runtime directories."),
      HelpParagraph(
          "  3. Kill all remaining run_cvd processes and their subprocesses."),
      HelpParagraph("  4. Release host resources previously used by any "
                    "untracked devices."),
      HelpParagraph(
          fmt::format("These steps are a best-effort attempt at resetting CVD "
                      "to a clean state, but it may sometimes not be enough. "
                      "In those cases the best course of action might be to "
                      "reboot the system and then delete the '{}' directory.",
                      CvdDir())),
  };
}

Result<std::vector<Flag>> CvdResetCommandHandler::Flags(const CommandRequest&) {
  Flag y_flag = GflagsCompatFlag("yes", flags_.is_confirmed_by_flag)
                    .Alias("y")
                    .Help(
                        "Provide user confirmation in advance so the command "
                        "doesn't ask for it interactively.");
  Flag clean_runtime_dir_flag =
      GflagsCompatFlag("clean-runtime-dir", flags_.clean_runtime_dir)
          .Help(
              "Clean up the runtime directory for untracked devices (not "
              "members of any known instance group)");
  return std::vector<Flag>{y_flag, clean_runtime_dir_flag};
}

std::unique_ptr<CvdCommandHandler> NewCvdResetCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdResetCommandHandler(instance_manager));
}

}  // namespace cuttlefish
