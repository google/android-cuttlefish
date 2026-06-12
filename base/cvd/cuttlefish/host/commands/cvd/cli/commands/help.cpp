/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/help.h"

#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/request_context.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kHelpExamplesText[] = R"(Typical Usage Examples:

  Create from specification (Multi-Device / Config-driven):

    1. Create a JSON configuration file (e.g., spec.json) containing:
       {
         "instances": [
           {
             "name": "phone-ins-1",
             "disk": {
               "default_build": "@ab/aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug"
             }
           },
           {
             "name": "phone-ins-2",
             "disk": {
               "default_build": "@ab/aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug"
             }
           }
         ]
       }

    2. Launch the group:
       $ cvd create --config_file=spec.json

    3. View the state of the running fleet:
       $ cvd fleet

    4. View the log files:
       $ cvd logs
       $ cvd logs -p launcher.log

    5. Open the web UI at https://localhost:1443

    6. Interact with the device via ADB (auto-connected on every start if the
       ADB server is running)

    7. Control the lifecycle:
       $ cvd stop
       $ cvd start

    8. Gather all logs for troubleshooting:
       $ cvd bugreport

    9. Clean up and delete all resources:
       $ cvd remove

  Create from local source (Platform Developer / AOSP build):

    1. Build Android in your terminal (ensuring ANDROID_HOST_OUT and
       ANDROID_PRODUCT_OUT are set in the environment).

    2. Launch the device:
       $ cvd create

    3. After making changes, rebuild and restart:
       $ cvd stop
       $ m  # Rebuild android
       $ cvd start

  Environment Cleanup (Last Resort):
    If virtual devices become unresponsive, or if 'cvd remove' fails to fully
    clean up the environment, use 'cvd reset' to forcefully terminate all
    Cuttlefish-related background processes and free up host resources:
       $ cvd reset)";

constexpr char kSummaryHelpText[] =
    "Used to display help information for other commands";

void PrintHandler(std::stringstream& help_message,
                  const CvdCommandHandler& handler) {
  help_message << "\t" << absl::StrJoin(handler.CmdList(), ", ") << " - ";
  help_message << handler.SummaryHelp() << "\n\n";
}

Result<CommandRequest> GetLookupRequest(const std::string& arg) {
  auto builder = CommandRequestBuilder().AddArguments({"cvd", arg});
  return CF_EXPECT(std::move(builder).Build());
}

}  // namespace

CvdHelpHandler::CvdHelpHandler(
    const std::vector<std::unique_ptr<CvdCommandHandler>>& request_handlers)
    : request_handlers_(request_handlers) {}

Result<void> CvdHelpHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  if (args.empty()) {
    std::cout << TopLevelHelp();
  } else {
    std::cout << CF_EXPECT(SubCommandHelp(request));
  }

  return {};
}

cvd_common::Args CvdHelpHandler::CmdList() const { return {"help"}; }

std::string CvdHelpHandler::SummaryHelp() const { return kSummaryHelpText; }

bool CvdHelpHandler::RequiresHostConfiguration() const { return false; }

std::vector<HelpParagraph> CvdHelpHandler::Description() const {
  return {
      HelpParagraph("Example usage:"),
      HelpParagraph::Raw(
          "  cvd help - displays summary help for available commands"),
      HelpParagraph::Raw("  cvd help <command> - displays more detailed help "
                         "for the specific command"),
  };
}

std::string CvdHelpHandler::TopLevelHelp() {
  std::vector<HelpParagraph> paragraphs;
  paragraphs.emplace_back("Cuttlefish Virtual Device (CVD) CLI.");
  paragraphs.emplace_back(
      "Cuttlefish is a configurable virtual Android device offering "
      "high-fidelity Android emulation for platform and app development and "
      "automated testing.");

  paragraphs.emplace_back(HelpParagraph::Raw(
      R"(Device Organization:
  Cuttlefish devices are organized into Instance Groups and Instances.
  - An Instance represents a single virtual Android device.
  - An Instance Group is a logical collection of one or more Instances created
    and managed together.
  - Group and Instance Naming:
    - Each group and instance has a name. Names can be user-provided or
      automatically generated by cvd.
    - Group names must be unique across the host for the current user.
    - Instance names must be unique within their containing group.)"));

  paragraphs.emplace_back(HelpParagraph::Raw(
      R"(Cuttlefish devices lifecycle:
  - create:     Cuttlefish devices must be "created" (`cvd create` command)
    before they can be used. The instance group directory structure is created,
    host resources like virtual networks and instance ids are allocated and
    build artifacts are fetched during this step. Cuttlefish devices are
    automatically started at the end of the creation process unless the user
    passes the --nostart option.
  - start|stop  Turn the virtual devices ON or OFF respectively. These actions
    can be performed repeatedly just like in a physical Android device. Virtual
    device state and disks are preserved across a stop-start cycle.
  - remove:     The opposite of create, this action completely removes the
    devices from the system. Logs and virtual disks in particular are deleted
    during this step. Host resources, context ids and cuttlefish names are also
    released.)"));

  paragraphs.emplace_back(HelpParagraph::Raw(
      R"(Selector Arguments:
  Many commands act on an existing instance or group. If only one group or
  instance exists, cvd will select that one as the command's target, otherwise
  it will ask you to select one from a list. Selector options can be used
  to explicitly select a target and avoid the interactive selection step:
    cvd -group_name <group_name> <command>
    cvd -instance_name <instance_name> <command>)"));

  paragraphs.emplace_back("Build Fetching and Caching:");
  paragraphs.emplace_back(
      "cvd can automatically download Android builds from the Android Build "
      "servers (e.g., "
      "\"@ab/aosp-android-latest-release/"
      "aosp_cf_x86_64_only_phone-userdebug\"). cvd fetches the required host "
      "tools and device images, caching them locally to accelerate future "
      "device creation.");

  paragraphs.emplace_back(HelpParagraph::Raw(
      R"(Usage:
    cvd [selector/global options] <command> [args]
    cvd help [<command> [args]])"));

  std::stringstream help_message;
  help_message << FormatHelpText(paragraphs);

  help_message << "Global Options:\n";
  std::string verbosity_val = "INFO";
  std::vector<Flag> global_flags = {
      GflagsCompatFlag("verbosity", verbosity_val)
          .Help("Adjust Cvd verbosity level. LEVEL is one of ERROR, WARNING, "
                "INFO, DEBUG, VERBOSE."),
  };
  help_message << FormatFlagsHelp(global_flags);

  help_message << "Selector Options:\n";
  selector::SelectorOptions dummy_selector_options;
  auto selector_flags =
      selector::BuildCommonSelectorFlags(dummy_selector_options);
  help_message << FormatFlagsHelp(selector_flags);

  help_message << "Commands (cvd help <command> for more information):\n\n";
  for (const auto& handler : request_handlers_) {
    if (!handler->RequiresDeviceExists()) {
      PrintHandler(help_message, *handler);
    }
  }

  help_message << "\n  Device-Specific Commands (cvd help <command> for more "
                  "information):\n\n";
  for (const auto& handler : request_handlers_) {
    if (handler->RequiresDeviceExists()) {
      PrintHandler(help_message, *handler);
    }
  }

  help_message << FormatHelpText({HelpParagraph::Raw(kHelpExamplesText)});

  return help_message.str();
}

Result<std::string> CvdHelpHandler::SubCommandHelp(
    const CommandRequest& request) {
  const std::vector<std::string>& args = request.SubcommandArguments();
  CF_EXPECT(!args.empty(),
            "Cannot process subcommand help without valid subcommand argument");
  CommandRequest lookup_request = CF_EXPECT(GetLookupRequest(args.front()));
  auto handler = CF_EXPECT(RequestHandler(lookup_request, request_handlers_));

  // Create new command with "<subcmd> --help" instead of "help <subcmd>"
  CommandRequest subcmd_request =
      CF_EXPECT(CommandRequestBuilder()
                    .AddArguments(args)
                    .AddArguments({"--help"})
                    .SetEnv(request.Env())
                    .SetSelectorOptions(request.Selectors())
                    .Build());

  std::stringstream help_message;
  help_message << CF_EXPECT(handler->DetailedHelp(subcmd_request)) << std::endl;
  return help_message.str();
}

std::unique_ptr<CvdCommandHandler> NewCvdHelpHandler(
    const std::vector<std::unique_ptr<CvdCommandHandler>>& server_handlers) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdHelpHandler(server_handlers));
}

}  // namespace cuttlefish
