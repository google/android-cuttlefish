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

#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"

#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/interruptible_terminal.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/status_fetcher.h"

namespace cuttlefish {
namespace selector {
namespace {

std::string GroupDisplay(const std::vector<LocalInstanceGroup>& groups,
                         const TerminalColors& colors) {
  std::stringstream result;
  int group_index = 0;
  for (const LocalInstanceGroup& group : groups) {
    fmt::print(result, "{}[{}]{} - ", colors.Cyan(), group_index,
               colors.Reset());
    fmt::print(result, "{} (created: {})\n", group.GroupName(),
               Format(group.StartTime()));

    for (const LocalInstance& instance : group.Instances()) {
      result << "\t";
      fmt::print(result, "{}-{} (id : {} | status : {})\n", group.GroupName(),
                 instance.Name(), instance.Id(),
                 HumanFriendlyStateName(instance.State()));
    }

    group_index++;
  }
  return result.str();
}

Result<int> PromptForSelection(const int max_selection) {
  std::unique_ptr<InterruptibleTerminal> terminal =
      std::make_unique<InterruptibleTerminal>();

  TerminalColors colors(isatty(2));

  int selection = -1;
  while (selection < 0 || selection > max_selection) {
    fmt::print(std::cout, "\nSelect {}[0,{}]{}: ", colors.Cyan(), max_selection,
               colors.Reset());
    std::cout << std::flush;
    std::string input_line = CF_EXPECT(terminal->ReadLine());
    if (!absl::SimpleAtoi(input_line, &selection)) {
      selection = -1;
      fmt::print(std::cerr, "Selection \"{}{}{}\" is not a valid.\n",
                 colors.BoldRed(), input_line, colors.Reset());
      continue;
    }
    if (selection > max_selection) {
      fmt::print(std::cerr,
                 "Selection \"{}{}{}\" is beyond the allowed range.\n",
                 colors.BoldRed(), selection, colors.Reset());
      continue;
    }
  }
  return selection;
}

Result<LocalInstanceGroup> PromptUserForGroup(
    const InstanceManager& instance_manager) {
  const std::vector<LocalInstanceGroup> groups =
      CF_EXPECT(instance_manager.FindGroups({}));
  const TerminalColors colors(isatty(1));
  std::cout << GroupDisplay(groups, colors);

  const int selection = CF_EXPECT(PromptForSelection(groups.size() - 1));
  auto group_filter = InstanceDatabase::Filter{
      .group_name = groups[selection].GroupName(),
  };

  return CF_EXPECT(instance_manager.FindGroup(group_filter));
}

std::string FormatInstanceSelectionMenu(
    const std::vector<std::pair<
        LocalInstanceGroup, std::vector<LocalInstance>>>& instances_by_group,
    const TerminalColors& colors) {
  std::stringstream result;
  int global_instance_index = 0;
  for (const auto& [group, instances] : instances_by_group) {
    fmt::print(result, "{} (created: {})\n", group.GroupName(),
               Format(group.StartTime()));
    for (const LocalInstance& instance : instances) {
      fmt::print(result, "\t{}[{}]{} - {}-{} (id : {} | status: {})\n",
                 colors.Cyan(), global_instance_index, colors.Reset(),
                 group.GroupName(), instance.Name(), instance.Id(),
                 HumanFriendlyStateName(instance.State()));
      global_instance_index++;
    }
  }
  return result.str();
}

Result<std::pair<LocalInstance, LocalInstanceGroup>> PromptUserForInstance(
    const std::vector<std::pair<LocalInstanceGroup,
                                std::vector<LocalInstance>>>& found_instances) {
  std::vector<std::pair<LocalInstance, LocalInstanceGroup>> flat_instances;
  for (const auto& [group, instances] : found_instances) {
    for (const LocalInstance& instance : instances) {
      flat_instances.push_back({instance, group});
    }
  }

  CF_EXPECT(!flat_instances.empty(), "No instances available");

  const TerminalColors colors(isatty(1));
  std::cout << FormatInstanceSelectionMenu(found_instances, colors);

  const int selection =
      CF_EXPECT(PromptForSelection(flat_instances.size() - 1));
  return flat_instances[selection];
}

}  // namespace

InstanceDatabase::Filter BuildFilterFromSelectors(
    const SelectorOptions& selectors) {
  InstanceDatabase::Filter filter;
  filter.group_name = selectors.group_name;
  if (selectors.instance_names) {
    const std::vector<std::string> per_instance_names =
        selectors.instance_names.value();
    for (const auto& per_instance_name : per_instance_names) {
      filter.instance_names.insert(per_instance_name);
    }
  }
  return filter;
}

Result<LocalInstanceGroup> SelectGroup(const InstanceManager& instance_manager,
                                       const CommandRequest& request) {
  const InstanceDatabase::Filter filter =
      BuildFilterFromSelectors(request.Selectors());
  std::vector<LocalInstanceGroup> groups =
      CF_EXPECT(instance_manager.FindGroups(filter));
  CF_EXPECT(!groups.empty(), "No instance groups available");
  if (groups.size() == 1) {
    return groups.front();
  }
  CF_EXPECT(
      isatty(0),
      "Multiple groups found. Narrow the selection with selector arguments.");
  return CF_EXPECT(PromptUserForGroup(instance_manager));
}

Result<std::pair<LocalInstance, LocalInstanceGroup>> SelectInstance(
    const InstanceManager& instance_manager, const CommandRequest& request) {
  const InstanceDatabase::Filter filter =
      BuildFilterFromSelectors(request.Selectors());
  std::vector<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>
      found_instances = CF_EXPECT(instance_manager.FindInstances(filter));
  CF_EXPECT(!found_instances.empty(), "No instances available");
  if (found_instances.size() == 1 &&
      found_instances.front().second.size() == 1) {
    auto [group, instances] = found_instances.front();
    return std::make_pair(instances.front(), group);
  }
  CF_EXPECT(isatty(0),
            "Multiple instances found.  Narrow the selection with selector "
            "arguments.");
  return CF_EXPECT(PromptUserForInstance(found_instances));
}

Result<std::vector<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>>
SelectInstances(const InstanceManager& instance_manager,
                const CommandRequest& request) {
  const InstanceDatabase::Filter filter =
      BuildFilterFromSelectors(request.Selectors());
  return CF_EXPECT(instance_manager.FindInstances(filter));
}

}  // namespace selector
}  // namespace cuttlefish
