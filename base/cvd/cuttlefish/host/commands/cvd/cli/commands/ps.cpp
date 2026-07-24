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

#include "cuttlefish/host/commands/cvd/cli/commands/ps.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fmt/format.h"
#include "json/value.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database_types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kPsSubcmd[] = "ps";
constexpr char kSummaryHelpText[] =
    "lists active devices in a human-readable table format";
constexpr char kHelpMessage[] = R"(
usage: cvd ps [--help]

  cvd ps will list the active devices in a clean table format.
)";

const std::map<PsColumns, std::string_view> kHeaders = {
    {PsColumns::kGroups, "GROUPS"},
    {PsColumns::kNames, "NAMES"},
    {PsColumns::kId, "ID"},
    {PsColumns::kStatus, "STATUS"},
    {PsColumns::kCreated, "CREATED"},
    {PsColumns::kAdbSerial, "ADB_SERIAL"},
    {PsColumns::kWebAccess, "WEB_ACCESS"},
};

void PrintTable(const std::map<PsColumns, std::string_view>& headers,
                const std::vector<PsRow>& rows) {
  std::map<PsColumns, size_t> col_widths;
  for (const std::pair<const PsColumns, std::string_view>& pair : headers) {
    col_widths[pair.first] = pair.second.size();
  }

  for (const PsRow& row : rows) {
    for (const std::pair<const PsColumns, std::string>& pair : row) {
      col_widths[pair.first] =
          std::max(col_widths[pair.first], pair.second.size());
    }
  }

  // Print headers
  for (const std::pair<const PsColumns, std::string_view>& pair : headers) {
    std::cout << std::left << std::setw(col_widths[pair.first] + 3)
              << pair.second;
  }
  std::cout << "\n";

  // Print rows
  for (const PsRow& row : rows) {
    for (const std::pair<const PsColumns, std::string_view>& pair : headers) {
      std::string val = "";
      std::map<PsColumns, std::string>::const_iterator it =
          row.find(pair.first);
      if (it != row.end()) {
        val = it->second;
      }
      std::cout << std::left << std::setw(col_widths[pair.first] + 3) << val;
    }
    std::cout << "\n";
  }
}

}  // namespace

CvdPsCommandHandler::CvdPsCommandHandler(InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

std::vector<std::string> CvdPsCommandHandler::CmdList() const {
  return {kPsSubcmd};
}

std::string CvdPsCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdPsCommandHandler::DetailedHelp(const CommandRequest&) {
  return kHelpMessage;
}

Result<void> CvdPsCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags({}, args, {.fail_on_unexpected_argument = true}));

  std::vector<LocalInstanceGroup> all_groups =
      CF_EXPECT(instance_manager_.FindGroups({}));

  std::vector<PsRow> rows;

  for (LocalInstanceGroup& group : all_groups) {
    for (LocalInstance& instance : group.Instances()) {
      rows.push_back(InstanceToRow(group, instance));
    }
  }

  PrintTable(kHeaders, rows);
  return {};
}

PsRow CvdPsCommandHandler::InstanceToRow(const LocalInstanceGroup& group,
                                         LocalInstance& instance) const {
  PsRow row;

  row[PsColumns::kGroups] = group.GroupName();
  row[PsColumns::kNames] = instance.Name();
  row[PsColumns::kId] = std::to_string(instance.Id());

  // Fetch live status
  const Result<Json::Value> status_json_res = instance.FetchStatus();
  std::string status_str = "Unknown (Fetch Failed)";
  std::string adb_serial_str = "-";
  std::string web_access_str = "-";

  if (status_json_res.has_value()) {
    const Json::Value status_json = *status_json_res;
    status_str = status_json["status"].asString();

    if (instance.IsActive()) {
      if (status_json.isMember("adb_port")) {
        adb_serial_str =
            fmt::format("localhost:{}", status_json["adb_port"].asInt());
      }
      if (status_json.isMember("web_access")) {
        web_access_str = status_json["web_access"].asString();
      }
    }
  }

  row[PsColumns::kStatus] = status_str;
  row[PsColumns::kCreated] = Format(group.StartTime());
  row[PsColumns::kAdbSerial] = adb_serial_str;
  row[PsColumns::kWebAccess] = web_access_str;

  return row;
}

}  // namespace cuttlefish
