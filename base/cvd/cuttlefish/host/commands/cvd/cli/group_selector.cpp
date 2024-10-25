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

#include "host/commands/cvd/cli/group_selector.h"

#include <sstream>
#include <string>

#include <android-base/parseint.h>

#include "host/commands/cvd/cli/interruptible_terminal.h"
#include "host/commands/cvd/instances/instance_group_record.h"
#include "host/commands/cvd/cli/selector/selector_constants.h"
#include "host/commands/cvd/cli/utils.h"

namespace cuttlefish {
namespace {

std::string SelectionMenu(
    const std::vector<selector::LocalInstanceGroup>& groups) {
  // Multiple instance groups found, please choose one:
  //   [i] : group_name (created: TIME)
  //      <a> instance0.device_name() (id: instance_id)
  //      <b> instance1.device_name() (id: instance_id)
  std::stringstream ss;
  ss << "Multiple instance groups found, please choose one:" << std::endl;
  int group_idx = 0;
  for (const auto& group : groups) {
    fmt::print(ss, "  [{}] : {} (created: {})\n", group_idx, group.GroupName(),
               selector::Format(group.StartTime()));
    char instance_idx = 'a';
    for (const auto& instance : group.Instances()) {
      fmt::print(ss, "    <{}> {}-{} (id : {})\n", instance_idx++,
                 group.GroupName(), instance.name(), instance.id());
    }
    group_idx++;
  }
  return ss.str();
}

Result<selector::LocalInstanceGroup> PromptUserForGroup(
    InstanceManager& instance_manager, const CommandRequest& request,
    const cvd_common::Envs& envs,
    const selector::SelectorOptions& selector_options) {
  // show the menu and let the user choose
  std::vector<selector::LocalInstanceGroup> groups =
      CF_EXPECT(instance_manager.FindGroups(selector::Queries{}));
  auto menu = SelectionMenu(groups);

  std::cout << menu << "\n";
  std::unique_ptr<InterruptibleTerminal> terminal_ =
      std::make_unique<InterruptibleTerminal>();

  TerminalColors colors(isatty(2));
  while (true) {
    std::string input_line = CF_EXPECT(terminal_->ReadLine());
    int selection = -1;
    std::string chosen_group_name;
    if (android::base::ParseInt(input_line, &selection)) {
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
      chosen_group_name = android::base::Trim(input_line);
    }

    InstanceManager::Queries extra_queries{
        {selector::kGroupNameField, chosen_group_name}};
    auto instance_group_result =
        instance_manager.SelectGroup(selector_options, envs, extra_queries);
    if (instance_group_result.ok()) {
      return instance_group_result;
    }
    fmt::print(std::cerr,
               "\n  Failed to find a group whose name is {}\"{}\"{}\n\n",
               colors.BoldRed(), chosen_group_name, colors.Reset());
  }
}

}  // namespace

Result<selector::LocalInstanceGroup> SelectGroup(
    InstanceManager& instance_manager, const CommandRequest& request) {
  auto has_groups = CF_EXPECT(instance_manager.HasInstanceGroups());
  CF_EXPECT(std::move(has_groups), "No instance groups available");
  const cvd_common::Envs& env = request.Env();
  const auto& selector_options = request.Selectors();
  auto group_selection_result =
      instance_manager.SelectGroup(selector_options, env);
  if (group_selection_result.ok()) {
    return CF_EXPECT(std::move(group_selection_result));
  }
  CF_EXPECT(isatty(0),
            "Multiple groups found. Narrow the selection with selector "
            "arguments or run in an interactive terminal.");
  return CF_EXPECT(
      PromptUserForGroup(instance_manager, request, env, selector_options));
}

}  // namespace cuttlefish
