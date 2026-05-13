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
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/host/commands/cvd/cli/interruptible_terminal.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"

namespace cuttlefish {
namespace selector {
namespace {

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

std::string SelectionMenu(const std::vector<LocalInstanceGroup>& groups) {
  // Multiple instance groups found, please choose one:
  //   [i] : group_name (created: TIME)
  //      <a> instance0.device_name() (id: instance_id)
  //      <b> instance1.device_name() (id: instance_id)
  std::stringstream ss;
  ss << "Multiple groups found, please choose one:" << std::endl;
  int group_idx = 0;
  for (const auto& group : groups) {
    fmt::print(ss, "  [{}] : {} (created: {})\n", group_idx, group.GroupName(),
               Format(group.StartTime()));
    char instance_idx = 'a';
    for (const auto& instance : group.Instances()) {
      fmt::print(ss, "    <{}> {}-{} (id : {})\n", instance_idx++,
                 group.GroupName(), instance.name(), instance.id());
    }
    group_idx++;
  }
  return ss.str();
}

Result<LocalInstanceGroup> PromptUserForGroup(
    const InstanceManager& instance_manager, const CommandRequest& request,
    InstanceDatabase::Filter filter) {
  // show the menu and let the user choose
  std::vector<LocalInstanceGroup> groups =
      CF_EXPECT(instance_manager.FindGroups({}));
  std::string menu = SelectionMenu(groups);

  std::cout << menu << "\n";
  std::unique_ptr<InterruptibleTerminal> terminal_ =
      std::make_unique<InterruptibleTerminal>();

  TerminalColors colors(isatty(2));
  while (true) {
    std::string input_line = CF_EXPECT(terminal_->ReadLine());
    int selection = -1;
    std::string chosen_group_name;
    if (absl::SimpleAtoi(input_line, &selection)) {
      const int n_groups = groups.size();
      if (n_groups <= selection || selection < 0) {
        fmt::print(std::cerr,
                   "\n  Selection {}{}{} is beyond the range {}[0, {}]{}\n\n",
                   colors.BoldRed(), selection, colors.Reset(), colors.Cyan(),
                   n_groups - 1, colors.Reset());
        continue;
      }
      chosen_group_name = groups[selection].GroupName();
    } else {
      chosen_group_name = std::string(absl::StripAsciiWhitespace(input_line));
    }

    filter.group_name = chosen_group_name;
    Result<LocalInstanceGroup> instance_group_result =
        instance_manager.FindGroup(filter);
    if (instance_group_result.ok()) {
      return instance_group_result;
    }
    fmt::print(std::cerr,
               "\n  Failed to find a group whose name is {}\"{}\"{}\n\n",
               colors.BoldRed(), chosen_group_name, colors.Reset());
  }
}

Result<std::pair<LocalInstance, LocalInstanceGroup>> PromptUserForInstance(
    const InstanceManager& instance_manager, const CommandRequest& request,
    InstanceDatabase::Filter filter) {
  // TODO CJR: adapt or clone PromptUserForGroup logic, but only allow instance
  // selection
  return CF_ERR("TODO CJR implement");
}

}  // namespace

Result<LocalInstanceGroup> SelectGroup(const InstanceManager& instance_manager,
                                       const CommandRequest& request) {
  const InstanceDatabase::Filter filter =
      CF_EXPECT(BuildFilterFromSelectors(request.Selectors()));
  std::vector<LocalInstanceGroup> groups;
  if (filter.Empty()) {  // try to default
    groups = CF_EXPECT(instance_manager.FindGroups({}));
  } else {
    groups = CF_EXPECT(instance_manager.FindGroups(filter));
  }
  CF_EXPECT(!groups.empty(), "No instance groups available");
  if (groups.size() == 1) {
    return groups.front();
  }
  CF_EXPECT(isatty(0),
            "Multiple groups found. Narrow the selection with selector "
            "arguments or run in an interactive terminal.");
  return CF_EXPECT(
      PromptUserForGroup(instance_manager, request, std::move(filter)));
}

Result<std::pair<LocalInstance, LocalInstanceGroup>> SelectInstance(
    const InstanceManager& instance_manager, const CommandRequest& request) {
  const InstanceDatabase::Filter filter =
      CF_EXPECT(BuildFilterFromSelectors(request.Selectors()));
  std::vector<std::pair<LocalInstance, LocalInstanceGroup>> instances;
  if (filter.Empty()) {  // try to default
    instances = CF_EXPECT(instance_manager.FindInstances({}));
  } else {
    instances = CF_EXPECT(instance_manager.FindInstances(filter));
  }
  CF_EXPECT(!instances.empty(), "No instances available");
  if (instances.size() == 1) {
    return instances.front();
  }
  CF_EXPECT(isatty(0),
            "Multiple instances found.  Narrow the selection with selector "
            "arguments or run in an interactive terminal");
  return CF_EXPECT(
      PromptUserForInstance(instance_manager, request, std::move(filter)));
}

}  // namespace selector
}  // namespace cuttlefish
