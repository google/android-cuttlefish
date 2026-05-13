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
#include <memory>
#include <string>
#include <vector>

#include <json/value.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
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
    : instance_manager_(instance_manager) {}

cvd_common::Args CvdFleetCommandHandler::CmdList() const {
  return {kFleetSubcmd};
}

std::string CvdFleetCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdFleetCommandHandler::DetailedHelp(
    const CommandRequest& request) const {
  return kHelpMessage;
}

Result<void> CvdFleetCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags({UnexpectedArgumentGuard()}, args));

  auto all_groups = CF_EXPECT(instance_manager_.FindGroups({}));
  Json::Value groups_json(Json::arrayValue);
  for (auto& group : all_groups) {
    groups_json.append(CF_EXPECT(group.FetchStatus()));
  }
  Json::Value output_json(Json::objectValue);
  output_json["groups"] = groups_json;

  std::cout << output_json.toStyledString();

  return {};
}

std::unique_ptr<CvdCommandHandler> NewCvdFleetCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdFleetCommandHandler(instance_manager));
}

}  // namespace cuttlefish
