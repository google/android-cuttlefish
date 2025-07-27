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

#include "cuttlefish/host/commands/cvd/cli/commands/bugreport.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include "android-base/file.h"
#include "android-base/logging.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/interruptible_terminal.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"
#include "cuttlefish/host/libs/zip/zip_file.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Run cvd bugreport --help for command description";

// Accepts a copy of the args to not modify the original.
Result<std::string> OutputFileFromArgs(cvd_common::Args args) {
  // This flag must match the one defined in
  // //cuttlefish/host/commands/host_bugreport/main.cc
  std::string output = "host_bugreport.zip";
  std::vector<Flag> flags = {
      GflagsCompatFlag("output", output),
  };
  CF_EXPECT(ConsumeFlags(flags, args));
  return output;
}

Result<void> AddFetchLogIfPresent(const LocalInstanceGroup& instance_group,
                                  const std::string& output_file) {
  std::string fetch_log_path = instance_group.ProductOutPath() + "/fetch.log";
  if (!FileExists(fetch_log_path)) {
    // The fetch log is in the parent of the host artifacts path when cvd create
    // --config_file was used.
    fetch_log_path =
        android::base::Dirname(instance_group.HostArtifactsPath()) +
        "/fetch.log";
  }
  if (!FileExists(fetch_log_path)) {
    // There will be no fetch log when running from local sources
    return {};
  }
  LOG(INFO) << "Attaching fetch.log to report";
  WritableZip archive = CF_EXPECT(ZipOpenReadWrite(output_file));
  CF_EXPECT(AddFileAt(archive, fetch_log_path, "fetch.log"));
  CF_EXPECT(WritableZip::Finalize(std::move(archive)));
  return {};
}

class CvdBugreportCommandHandler : public CvdCommandHandler {
 public:
  CvdBugreportCommandHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  Result<void> HandleHelp(const cvd_common::Envs& env,
                          const cvd_common::Args& cmd_args,
                          const CommandRequest& request);
  cvd_common::Args CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  InstanceManager& instance_manager_;
  using BinGeneratorType = std::function<Result<std::string>(
      const std::string& host_artifacts_path)>;
  std::unique_ptr<InterruptibleTerminal> terminal_ = nullptr;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
};

CvdBugreportCommandHandler::CvdBugreportCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdBugreportCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));

  std::vector<std::string> cmd_args = request.SubcommandArguments();
  cvd_common::Envs env = request.Env();

  std::string android_host_out;
  std::string home = CF_EXPECT(SystemWideUserHome());

  if (CF_EXPECT(HasHelpFlag(cmd_args))) {
    CF_EXPECT(HandleHelp(env, cmd_args, request));
    return {};
  }

  std::string output_file =
      CF_EXPECT(OutputFileFromArgs(cmd_args), "Failed to parse output flag");

  bool has_instance_groups = CF_EXPECT(instance_manager_.HasInstanceGroups());
  CF_EXPECTF(!!has_instance_groups, "{}", NoGroupMessage(request));

  auto instance_group =
      CF_EXPECT(selector::SelectGroup(instance_manager_, request));
  android_host_out = instance_group.HostArtifactsPath();
  home = instance_group.HomeDir();
  env["HOME"] = home;
  env[kAndroidHostOut] = android_host_out;
  auto bin_path = ConcatToString(android_host_out, "/bin/", kHostBugreportBin);

  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = home,
                                            .args = cmd_args,
                                            .envs = env,
                                            .working_dir = CurrentDirectory(),
                                            .command_name = kHostBugreportBin};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  // Wait for the command to finish but ignore the result. The command will fail
  // for reasons like the device failing to initialize the home directory or
  // errors during fetch, which are still debuggable states that require a
  // report.
  (void)command.Start().Wait();

  auto result = AddFetchLogIfPresent(instance_group, output_file);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to add fetch log to bugreport: "
               << result.error().FormatForEnv();
  }

  return {};
}

Result<void> CvdBugreportCommandHandler::HandleHelp(
    const cvd_common::Envs& env, const cvd_common::Args& cmd_args,
    const CommandRequest& request) {
  std::string android_host_out = CF_EXPECT(AndroidHostPath(env));
  Command command = CF_EXPECT(
      ConstructCvdHelpCommand(kHostBugreportBin, env, cmd_args, request));

  siginfo_t infop;  // NOLINT(misc-include-cleaner)
  command.Start().Wait(&infop, WEXITED);

  CF_EXPECT(CheckProcessExitedNormally(infop));
  return {};
}

std::vector<std::string> CvdBugreportCommandHandler::CmdList() const {
  return {"bugreport", "host_bugreport", "cvd_host_bugreport"};
}

Result<std::string> CvdBugreportCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdBugreportCommandHandler::ShouldInterceptHelp() const { return false; }

Result<std::string> CvdBugreportCommandHandler::DetailedHelp(
    std::vector<std::string>& arguments) const {
  static constexpr char kDetailedHelpText[] =
      "Run cvd {} --help for full help text";
  std::string replacement = "<command>";
  if (!arguments.empty()) {
    replacement = arguments.front();
  }
  return fmt::format(kDetailedHelpText, replacement);
}

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdBugreportCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdBugreportCommandHandler(instance_manager));
}

}  // namespace cuttlefish
