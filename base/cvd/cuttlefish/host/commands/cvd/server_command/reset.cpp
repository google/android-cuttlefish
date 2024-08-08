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

#include "host/commands/cvd/server_command/reset.h"

#include <iostream>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/reset_client_utils.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Used to stop devices, optionally clean up instance files, and shut down "
    "the deprecated cvd server process";

constexpr char kDetailedHelpText[] = R"(usage: cvd reset <args>

* Warning: Cvd reset is an experimental implementation. When you are in panic,
cvd reset is the last resort.

args:
  --help                 Prints this message.
    help

  --device-by-cvd-only   Terminates devices that a cvd server started
                         This excludes the devices launched by "launch_cvd"
                         or "cvd_internal_start" directly (default: false)

  --clean-runtime-dir    Cleans up the runtime directory for the devices
                         Yet to be implemented. For now, if true, only if
                         stop_cvd supports --clear_instance_dirs and the
                         device could be stopped by stop_cvd, the flag takes
                         effects. (default: true)

  --yes                  Resets without asking the user confirmation.
   -y

description:

  1. Gracefully stops all devices that the cvd client can reach.
  2. Forcefully stops all run_cvd processes and their subprocesses.
  3. Kill the cvd server itself if unresponsive.
  4. Reset the states of the involved instance lock files
     -- If cvd reset stops a device, it resets the corresponding lock file.
  5. Optionally, cleans up the runtime files of the stopped devices.)";

struct ParsedFlags {
  bool clean_runtime_dir = true;
  bool device_by_cvd_only = false;
  bool is_confirmed_by_flag = false;
  std::optional<android::base::LogSeverity> log_level;
};

static Result<ParsedFlags> ParseResetFlags(cvd_common::Args subcmd_args) {
  if (subcmd_args.size() > 2 && subcmd_args.at(2) == "help") {
    // unfortunately, {FlagAliasMode::kFlagExact, "help"} is not allowed
    subcmd_args[2] = "--help";
  }

  ParsedFlags parsed_flags;
  std::string verbosity_flag_value;

  Flag y_flag = Flag()
                    .Alias({FlagAliasMode::kFlagExact, "-y"})
                    .Alias({FlagAliasMode::kFlagExact, "--yes"})
                    .Setter([&parsed_flags](const FlagMatch&) -> Result<void> {
                      parsed_flags.is_confirmed_by_flag = true;
                      return {};
                    });
  std::vector<Flag> flags{
      GflagsCompatFlag("device-by-cvd-only", parsed_flags.device_by_cvd_only),
      y_flag,
      GflagsCompatFlag("clean-runtime-dir", parsed_flags.clean_runtime_dir),
      GflagsCompatFlag("verbosity", verbosity_flag_value),
      UnexpectedArgumentGuard()};
  CF_EXPECT(ConsumeFlags(flags, subcmd_args));

  std::optional<android::base::LogSeverity> verbosity;
  if (!verbosity_flag_value.empty()) {
    verbosity = CF_EXPECT(EncodeVerbosity(verbosity_flag_value),
                          "invalid verbosity level");
  }
  parsed_flags.log_level = verbosity;
  return parsed_flags;
}

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

class CvdResetCommandHandler : public CvdServerHandler {
 public:
  CvdResetCommandHandler(InstanceManager& instance_manager)
      : instance_manager_(instance_manager) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == kResetSubcmd;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    auto invocation = ParseInvocation(request.Message());
    auto options = CF_EXPECT(ParseResetFlags(invocation.arguments));
    if (options.log_level) {
      SetMinimumVerbosity(options.log_level.value());
    }

    // cvd reset. Give one more opportunity
    if (!options.is_confirmed_by_flag && !GetUserConfirm()) {
      std::cout << "For more details: " << "  cvd reset --help" << std::endl;
      return {};
    }

    instance_manager_.CvdClear();
    // The instance database is obsolete now, clear it.
    auto instance_db_deleted = RemoveFile(InstanceDatabasePath());
    if (!instance_db_deleted) {
      LOG(ERROR) << "Error deleting instance database file";
    }

    // Any responsive cvd server process was stopped nicely when this process
    // began, kill any unresponsive ones left.
    auto server_kill_res = KillCvdServerProcess();
    if (!server_kill_res.ok()) {
      LOG(ERROR) << "Error trying to kill unresponsive cvd server: "
                 << server_kill_res.error().Message();
    }
    CF_EXPECT(KillAllCuttlefishInstances(
        {.cvd_server_children_only = options.device_by_cvd_only,
         .clear_instance_dirs = options.clean_runtime_dir}));
    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }
  cvd_common::Args CmdList() const override { return {kResetSubcmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  static constexpr char kResetSubcmd[] = "reset";
  InstanceManager& instance_manager_;
};

std::unique_ptr<CvdServerHandler> NewCvdResetCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdResetCommandHandler(instance_manager));
}

}  // namespace cuttlefish
