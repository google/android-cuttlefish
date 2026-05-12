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

namespace cuttlefish {
namespace selector {
namespace {

enum class DisplayBehavior {
  LabelGroup,
  LabelInstance,
};

Result<InstanceDatabase::Filter> BuildFilterFromSelectors(
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

std::string GroupDisplay(const std::vector<LocalInstanceGroup>& groups,
                         const DisplayBehavior behavior) {
  std::stringstream result;
  int group_index = 0;
  for (const LocalInstanceGroup& group : groups) {
    if (behavior == DisplayBehavior::LabelGroup) {
      fmt::print(result, "[{}] - ", group_index);
    }
    fmt::print(result, "{} (created: {})\n", group.GroupName(),
               Format(group.StartTime()));

    int instance_index = 0;
    for (const LocalInstance& instance : group.Instances()) {
      result << "\t";
      if (behavior == DisplayBehavior::LabelInstance) {
        fmt::print(result, "[{}] - ", instance_index);
      }
      fmt::print(result, "{}-{} (id : {})\n", group.GroupName(),
                 instance.Name(), instance.Id());

      instance_index++;
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
  std::cout << GroupDisplay(groups, DisplayBehavior::LabelGroup);

  const int selection = CF_EXPECT(PromptForSelection(groups.size() - 1));
  auto group_filter = InstanceDatabase::Filter{
      .group_name = groups[selection].GroupName(),
  };

  return CF_EXPECT(instance_manager.FindGroup(group_filter));
}

Result<std::pair<LocalInstance, LocalInstanceGroup>> PromptUserForInstance(
    const InstanceManager& instance_manager) {
  const LocalInstanceGroup group =
      CF_EXPECT(PromptUserForGroup(instance_manager));
  const std::vector<LocalInstance>& instances = group.Instances();
  if (instances.size() == 1) {
    fmt::print(std::cout,
               "Single instance in group {}, defaulting to that choice.\n",
               group.GroupName());
    return std::pair(instances.front(), group);
  }

  std::cout << GroupDisplay({group}, DisplayBehavior::LabelInstance);

  const int selection = CF_EXPECT(PromptForSelection(instances.size() - 1));
  auto instance_filter = InstanceDatabase::Filter{
      .group_name = group.GroupName(),
      .instance_names = {instances[selection].Name()},
  };
  return CF_EXPECT(instance_manager.FindInstanceWithGroup(instance_filter));
}

}  // namespace

Result<LocalInstanceGroup> SelectGroup(const InstanceManager& instance_manager,
                                       const CommandRequest& request) {
  const InstanceDatabase::Filter filter =
      CF_EXPECT(BuildFilterFromSelectors(request.Selectors()));
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
      CF_EXPECT(BuildFilterFromSelectors(request.Selectors()));
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
  return CF_EXPECT(PromptUserForInstance(instance_manager));
}

}  // namespace selector
}  // namespace cuttlefish
