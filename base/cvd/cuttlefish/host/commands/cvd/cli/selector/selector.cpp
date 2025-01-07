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

#include "host/commands/cvd/cli/selector/selector.h"

#include <sstream>
#include <string>

#include <android-base/parseint.h>

#include "common/libs/utils/users.h"
#include "host/commands/cvd/cli/interruptible_terminal.h"
#include "host/commands/cvd/cli/selector/device_selector_utils.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/instance_group_record.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace selector {
namespace {

Result<LocalInstanceGroup> GetDefaultGroup(
    const InstanceManager& instance_manager) {
  const auto all_groups = CF_EXPECT(instance_manager.FindGroups({}));
  if (all_groups.size() == 1) {
    return all_groups.front();
  }
  std::string system_wide_home = CF_EXPECT(SystemWideUserHome());
  auto group =
      CF_EXPECT(instance_manager.FindGroup({.home = system_wide_home}));
  return group;
}

Result<InstanceDatabase::Filter> BuildFilterFromSelectors(
    const SelectorOptions& selectors, const cvd_common::Envs& env) {
  InstanceDatabase::Filter filter;
  filter.home = OverridenHomeDirectory(env);
  filter.group_name = selectors.group_name;
  if (selectors.instance_names) {
    const auto per_instance_names = selectors.instance_names.value();
    for (const auto& per_instance_name : per_instance_names) {
      filter.instance_names.insert(per_instance_name);
    }
  }
  auto it = env.find(kCuttlefishInstanceEnvVarName);
  if (it != env.end()) {
    unsigned id;
    auto cuttlefish_instance = it->second;
    CF_EXPECT(android::base::ParseUint(cuttlefish_instance, &id));
  }

  return filter;
}

std::string SelectionMenu(const std::vector<LocalInstanceGroup>& groups) {
  // Multiple instance groups found, please choose one:
  //   [i] : group_name (created: TIME)
  //      <a> instance0.device_name() (id: instance_id)
  //      <b> instance1.device_name() (id: instance_id)
  std::stringstream ss;
  ss << "Multiple instance groups found, please choose one:" << std::endl;
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
    const cvd_common::Envs& envs, InstanceDatabase::Filter filter) {
  // show the menu and let the user choose
  std::vector<LocalInstanceGroup> groups =
      CF_EXPECT(instance_manager.FindGroups({}));
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

    filter.group_name = chosen_group_name;
    auto instance_group_result = instance_manager.FindGroup(filter);
    if (instance_group_result.ok()) {
      return instance_group_result;
    }
    fmt::print(std::cerr,
               "\n  Failed to find a group whose name is {}\"{}\"{}\n\n",
               colors.BoldRed(), chosen_group_name, colors.Reset());
  }
}

Result<LocalInstanceGroup> FindGroupOrDefault(
    const InstanceDatabase::Filter& filter,
    const InstanceManager& instance_manager) {
  if (filter.Empty()) {
    return CF_EXPECT(GetDefaultGroup(instance_manager));
  }
  auto groups = CF_EXPECT(instance_manager.FindGroups(filter));
  CF_EXPECT_EQ(groups.size(), 1u, "groups.size() = " << groups.size());
  return *(groups.cbegin());
}

Result<std::pair<LocalInstance, LocalInstanceGroup>> FindDefaultInstance(
    const InstanceManager& instance_manager) {
  auto group = CF_EXPECT(GetDefaultGroup(instance_manager));
  const auto instances = group.Instances();
  CF_EXPECT_EQ(instances.size(), 1u,
               "Default instance is the single instance in the default group.");
  return std::make_pair(*instances.cbegin(), group);
}

}  // namespace

Result<LocalInstanceGroup> SelectGroup(const InstanceManager& instance_manager,
                                       const CommandRequest& request) {
  auto has_groups = CF_EXPECT(instance_manager.HasInstanceGroups());
  CF_EXPECT(std::move(has_groups), "No instance groups available");
  const cvd_common::Envs& env = request.Env();
  const auto& selector_options = request.Selectors();
  InstanceDatabase::Filter filter =
      CF_EXPECT(BuildFilterFromSelectors(selector_options, request.Env()));
  auto group_selection_result = FindGroupOrDefault(filter, instance_manager);
  if (group_selection_result.ok()) {
    return CF_EXPECT(std::move(group_selection_result));
  }
  CF_EXPECT(isatty(0),
            "Multiple groups found. Narrow the selection with selector "
            "arguments or run in an interactive terminal.");
  return CF_EXPECT(
      PromptUserForGroup(instance_manager, request, env, std::move(filter)));
}

Result<std::pair<LocalInstance, LocalInstanceGroup>> SelectInstance(
    const InstanceManager& instance_manager, const CommandRequest& request) {
  InstanceDatabase::Filter filter =
      CF_EXPECT(BuildFilterFromSelectors(request.Selectors(), request.Env()));

  return filter.Empty()
             ? CF_EXPECT(FindDefaultInstance(instance_manager))
             : CF_EXPECT(instance_manager.FindInstanceWithGroup(filter));
}

}  // namespace selector
}  // namespace cuttlefish
