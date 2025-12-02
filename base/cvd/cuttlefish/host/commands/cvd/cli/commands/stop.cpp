/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/stop.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/metrics/metrics_orchestration.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Stop all instances in a group";

constexpr char kDetailedHelpText[] =
    R"""(
Stops all instances in an instance group

Usage:
cvd stop [--wait_for_launcher=SECONDS] [--clear_instance_dirs]

Stops a running cuttlefish instance group.

--wait_for_launcher=SECONDS    The number of seconds to wait for the launcher to
                     respond to the stop request. If SECONDS is 0 it will wait
                     indefinitely. Defaults to 5 seconds.

--clear_instance_dirs    If provided the instance directories will be deleted
                     after stopping.
)""";

struct StopFlags {
  size_t wait_for_launcher_secs = 5;
  bool clear_instance_dirs = false;
};
Result<StopFlags> ParseCommandFlags(cvd_common::Args& args) {
  StopFlags flag_values;
  std::vector<Flag> flags = {
      GflagsCompatFlag("wait_for_launcher", flag_values.wait_for_launcher_secs),
      GflagsCompatFlag("clear_instance_dirs", flag_values.clear_instance_dirs),
  };
  CF_EXPECT(ConsumeFlags(flags, args));
  return flag_values;
}

class CvdStopCommandHandler : public CvdCommandHandler {
 public:
  CvdStopCommandHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override { return {"stop", "stop_cvd"}; }
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override { return true; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  Result<std::string> GetBin(const std::string& host_artifacts_path) const;
  // whether the "bin" is cvd bins like stop_cvd or not (e.g. ln, ls, mkdir)
  // The information to fire the command might be different. This information
  // is about what the executable binary is and how to find it.
  struct BinPathInfo {
    std::string bin_;
    std::string bin_path_;
  };
  Result<BinPathInfo> CvdHelpBinPath(const std::string& subcmd,
                                     const cvd_common::Envs& envs) const;

  InstanceManager& instance_manager_;
  using BinGeneratorType = std::function<Result<std::string>(
      const std::string& host_artifacts_path)>;
};

CvdStopCommandHandler::CvdStopCommandHandler(InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdStopCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));
  std::vector<std::string> cmd_args = request.SubcommandArguments();

  bool has_help_flag = CF_EXPECT(HasHelpFlag(cmd_args));
  CF_EXPECT(!has_help_flag,
            "Help flag should be handled by global cvd as "
            "ShouldInterceptHelp() returns true");

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return CF_ERR(NoGroupMessage(request));
  }

  auto group = CF_EXPECT(selector::SelectGroup(instance_manager_, request));
  CF_EXPECT(group.HasActiveInstances(), "Selected group is not running");

  StopFlags flags = CF_EXPECT(ParseCommandFlags(cmd_args));
  std::optional<std::chrono::seconds> launcher_timeout;
  if (flags.wait_for_launcher_secs > 0) {
    launcher_timeout.emplace(flags.wait_for_launcher_secs);
  }
  Result<void> stop_outcome = instance_manager_.StopInstanceGroup(
      group, launcher_timeout,
      flags.clear_instance_dirs ? InstanceDirActionOnStop::Clear
                                : InstanceDirActionOnStop::Keep);

  GatherVmStopMetrics(group);

  CF_EXPECT(std::move(stop_outcome));
  return {};
}

Result<std::string> CvdStopCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdStopCommandHandler::DetailedHelp(
    std::vector<std::string>& arguments) const {
  return kDetailedHelpText;
}

Result<CvdStopCommandHandler::BinPathInfo>
CvdStopCommandHandler::CvdHelpBinPath(const std::string& subcmd,
                                      const cvd_common::Envs& envs) const {
  auto tool_dir_path = CF_EXPECT(AndroidHostPath(envs));
  auto bin_path_base = CF_EXPECT(GetBin(tool_dir_path));
  // no need of executable directory. Will look up by PATH
  // bin_path_base is like ln, mkdir, etc.
  return BinPathInfo{
      .bin_ = bin_path_base,
      .bin_path_ = tool_dir_path.append("/bin/").append(bin_path_base),
  };
}

Result<std::string> CvdStopCommandHandler::GetBin(
    const std::string& host_artifacts_path) const {
  return CF_EXPECT(HostToolTarget(host_artifacts_path).GetStopBinName());
}

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdStopCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdStopCommandHandler(instance_manager));
}

}  // namespace cuttlefish
