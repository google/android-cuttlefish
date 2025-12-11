/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/screen_recording.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "android-base/logging.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Record screen contents";
constexpr char kDetailedHelpText[] =
    R"""(
Records screen contents.

Usage:
    cvd [--group_name NAME] [--instance_name NAME] screen_recording list
        Print the paths to the existing recording files.

    cvd [--group_name NAME] [--instance_name NAME] screen_recording start [--timeout SECONDS]
    cvd [--group_name NAME] [--instance_name NAME] screen_recording stop [--timeout SECONDS]
        Start or Stop a recording.

Options:
    --timeout  The number of seconds to wait for the instance to respond to a start or stop request.
)""";

constexpr char kScreenRecordingCmd[] = "screen_recording";

constexpr char kListSubcmd[] = "list";
constexpr char kStartSubcmd[] = "start";
constexpr char kStopSubcmd[] = "stop";

constexpr int kDefaultWaitForLauncherSeconds = 5;

struct RecordingFlags {
  std::string subcmd;
  std::chrono::seconds timeout;
};

Result<RecordingFlags> ParseArgs(std::vector<std::string> args) {
  int timeout_secs = kDefaultWaitForLauncherSeconds;
  std::vector<Flag> flags = {
      GflagsCompatFlag("timeout", timeout_secs),
  };
  CF_EXPECT(ConsumeFlags(flags, args));
  CF_EXPECT(args.size() == 1, "Wrong number of arguments");
  const std::string& subcmd = args.front();
  CF_EXPECTF(
      subcmd == kStartSubcmd || subcmd == kStopSubcmd || subcmd == kListSubcmd,
      "Unrecognized command action: %s", subcmd);
  return RecordingFlags{
      .subcmd = subcmd,
      .timeout = std::chrono::seconds(timeout_secs),
  };
}

Result<void> StartStopRecording(const RecordingFlags& flags,
                                std::vector<LocalInstance>& instances) {
  bool some_failed = false;
  for (LocalInstance& instance : instances) {
    Result<void> result =
        flags.subcmd == kStartSubcmd
            ? instance.StartRecording(std::chrono::seconds(flags.timeout))
            : instance.StopRecording(std::chrono::seconds(flags.timeout));
    if (!result.ok()) {
      LOG(ERROR) << "Failed to " << flags.subcmd
                 << " screen recording for instance " << instance.name() << ": "
                 << result.error().FormatForEnv();
      some_failed = true;
    }
  }
  CF_EXPECT(!some_failed,
            "Some operations failed, see previous error for details");

  return {};
}

Result<void> ListRecordings(const LocalInstanceGroup& group,
                            std::vector<LocalInstance>& instances) {
  bool some_failed = false;
  Json::Value output(Json::arrayValue);
  for (LocalInstance& instance : instances) {
    Json::Value instance_json;
    instance_json["instance_name"] = instance.name();
    instance_json["group_name"] = instance.name();
    Json::Value recordings_array(Json::arrayValue);
    std::vector<std::string> recordings;
    Result<std::vector<std::string>> result = instance.ListRecordings();
    if (result.ok()) {
      recordings = std::move(*result);
    } else {
      LOG(ERROR) << "Failed to list screen recording for instance "
                 << instance.name() << ": " << result.error().FormatForEnv();
      some_failed = true;
    }
    for (const std::string& recording : recordings) {
      recordings_array.append(recording);
    }
    instance_json["recordings"] = recordings_array;
    output.append(instance_json);
  }
  std::cout << output.toStyledString();
  CF_EXPECT(!some_failed,
            "The operation failed for some instances, see previous error(s) "
            "for details");

  return {};
}

class ScreenRecordingCommandHandler : public CvdCommandHandler {
 public:
  ScreenRecordingCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    const std::vector<std::string>& args = request.SubcommandArguments();
    RecordingFlags flags = CF_EXPECT(ParseArgs(args));

    auto [group, instances] = CF_EXPECT(SelectInstances(request));

    if (flags.subcmd == kListSubcmd) {
      CF_EXPECT(ListRecordings(group, instances));
    } else {
      CF_EXPECT(StartStopRecording(flags, instances));
    }

    return {};
  }

  cvd_common::Args CmdList() const override { return {kScreenRecordingCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>
  SelectInstances(const CommandRequest& request) {
    if (request.Selectors().instance_names.has_value()) {
      auto [instance, group] =
          CF_EXPECT(selector::SelectInstance(instance_manager_, request));
      return std::pair<LocalInstanceGroup, std::vector<LocalInstance>>(
          group, {instance});
    } else {
      LocalInstanceGroup group =
          CF_EXPECT(selector::SelectGroup(instance_manager_, request));
      return std::pair<LocalInstanceGroup, std::vector<LocalInstance>>(
          group, group.Instances());
    }
  }

  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewScreenRecordingCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new ScreenRecordingCommandHandler(instance_manager));
}

}  // namespace cuttlefish

