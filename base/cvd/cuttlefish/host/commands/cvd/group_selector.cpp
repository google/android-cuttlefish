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

#include "host/commands/cvd/group_selector.h"

#include <sstream>
#include <string>

#include <android-base/parseint.h>

#include "host/commands/cvd/interruptible_terminal.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/utils.h"

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
    InstanceManager& instance_manager, const RequestWithStdio& request,
    const cvd_common::Envs& envs, const cvd_common::Args& selector_args) {
  // show the menu and let the user choose
  std::vector<selector::LocalInstanceGroup> groups =
      CF_EXPECT(instance_manager.FindGroups(selector::Queries{}));
  auto menu = SelectionMenu(groups);

  CF_EXPECT_EQ(WriteAll(request.Out(), menu + "\n"), (ssize_t)menu.size() + 1);
  std::unique_ptr<InterruptibleTerminal> terminal_ =
      std::make_unique<InterruptibleTerminal>(request.In());

  const bool is_tty = request.Err()->IsOpen() && request.Err()->IsATTY();
  while (true) {
    std::string question = "";
    CF_EXPECT_EQ(WriteAll(request.Out(), question), (ssize_t)question.size());

    std::string input_line = CF_EXPECT(terminal_->ReadLine());
    int selection = -1;
    std::string chosen_group_name;
    if (android::base::ParseInt(input_line, &selection)) {
      const int n_groups = groups.size();
      if (n_groups <= selection || selection < 0) {
        std::string out_of_range = fmt::format(
            "\n  Selection {}{}{} is beyond the range {}[0, {}]{}\n\n",
            TerminalColor(is_tty, TerminalColors::kBoldRed), selection,
            TerminalColor(is_tty, TerminalColors::kReset),
            TerminalColor(is_tty, TerminalColors::kCyan), n_groups - 1,
            TerminalColor(is_tty, TerminalColors::kReset));
        CF_EXPECT_EQ(WriteAll(request.Err(), out_of_range),
                     (ssize_t)out_of_range.size());
        continue;
      }
      chosen_group_name = groups[selection].GroupName();
    } else {
      chosen_group_name = android::base::Trim(input_line);
    }

    InstanceManager::Queries extra_queries{
        {selector::kGroupNameField, chosen_group_name}};
    auto instance_group_result =
        instance_manager.SelectGroup(selector_args, envs, extra_queries);
    if (instance_group_result.ok()) {
      return instance_group_result;
    }
    std::string cannot_find_group_name = fmt::format(
        "\n  Failed to find a group whose name is {}\"{}\"{}\n\n",
        TerminalColor(is_tty, TerminalColors::kBoldRed), chosen_group_name,
        TerminalColor(is_tty, TerminalColors::kReset));
    CF_EXPECT_EQ(WriteAll(request.Err(), cannot_find_group_name),
                 (ssize_t)cannot_find_group_name.size());
  }
}

}  // namespace

Result<selector::LocalInstanceGroup> SelectGroup(
    InstanceManager& instance_manager, const RequestWithStdio& request) {
  auto has_groups = CF_EXPECT(instance_manager.HasInstanceGroups());
  CF_EXPECT(std::move(has_groups), "No instance groups available");
  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());
  auto group_selection_result =
      instance_manager.SelectGroup(selector_args, envs);
  if (group_selection_result.ok()) {
    return CF_EXPECT(std::move(group_selection_result));
  }
  CF_EXPECT(request.In()->IsOpen() && request.In()->IsATTY(),
            "Multiple groups found. Narrow the selection with selector "
            "arguments or run in an interactive terminal.");
  return CF_EXPECT(
      PromptUserForGroup(instance_manager, request, envs, selector_args));
}

}  // namespace cuttlefish
