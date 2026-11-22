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

#include "cuttlefish/host/commands/cvd/cli/commands/fleet.h"

#include <iostream>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "json/value.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/status_fetcher.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

constexpr char kFleetSubcmd[] = "fleet";

constexpr char kSummaryHelpText[] =
    R"(lists active devices with relevant information)";

static constexpr char kHelpMessage[] = R"(
usage: cvd fleet [--help]

  cvd fleet will list the active devices with information.
)";

CvdFleetCommandHandler::CvdFleetCommandHandler(
    InstanceManager& instance_manager)
    : CvdFleetCommandHandler(instance_manager, std::cout) {}

CvdFleetCommandHandler::CvdFleetCommandHandler(
    InstanceManager& instance_manager, std::ostream& out)
    : instance_manager_(instance_manager), out_(out) {}

std::vector<std::string> CvdFleetCommandHandler::CmdList() const {
  return {kFleetSubcmd};
}

std::string CvdFleetCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdFleetCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  return kHelpMessage;
}

Result<void> CvdFleetCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags({}, args, {.fail_on_unexpected_argument = true}));

  CF_EXPECT(instance_manager_.PruneBrokenInstanceGroups());
  std::vector<LocalInstanceGroup> all_groups =
      CF_EXPECT(instance_manager_.FindGroups({}));
  Json::Value groups_json(Json::arrayValue);
  for (LocalInstanceGroup& group : all_groups) {
    const LocalInstanceGroup before = group.Clone();
    Result<Json::Value> group_status = group.FetchStatus();
    if (group_status.has_value()) {
      (void)instance_manager_.PersistInstanceStates(before, group);
      groups_json.append(std::move(*group_status));
      continue;
    }
    CF_EXPECT(instance_manager_.PruneBrokenInstanceGroups());
    std::vector<LocalInstanceGroup> reloaded = CF_EXPECT(
        instance_manager_.FindGroups({.group_name = group.GroupName()}));
    if (reloaded.empty() || reloaded.front().HomeDir() != group.HomeDir()) {
      continue;
    }
    LocalInstanceGroup latest = std::move(reloaded.front());
    if (!latest.HasActiveInstances()) {
      groups_json.append(CF_EXPECT(latest.FetchStatus()));
      continue;
    }
    const LocalInstanceGroup latest_before = latest.Clone();
    Json::Value instances_json(Json::arrayValue);
    for (LocalInstance& instance : latest.Instances()) {
      Result<Json::Value> inst_status = instance.FetchStatus();
      if (inst_status.has_value()) {
        instances_json.append(std::move(*inst_status));
      } else {
        instance.SetState(cvd::INSTANCE_STATE_UNREACHABLE);
        instances_json.append(FormatUnreachableInstanceStatus(instance));
      }
    }
    (void)instance_manager_.PersistInstanceStates(latest_before, latest);
    groups_json.append(latest.BuildStatusJson(std::move(instances_json)));
  }
  Json::Value output_json(Json::objectValue);
  output_json["groups"] = std::move(groups_json);

  out_ << output_json.toStyledString();

  return {};
}

}  // namespace cuttlefish
