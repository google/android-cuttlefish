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

#include "host/commands/cvd/selector/instance_group_record.h"

namespace cuttlefish {
std::string GroupSelector::Menu() {
  // List of Cuttlefish Instance Groups:
  //   [i] : group_name (created: TIME)
  //      <a> instance0.device_name() (id: instance_id)
  //      <b> instance1.device_name() (id: instance_id)
  std::stringstream ss;
  ss << "List of Cuttlefish Instance Groups:" << std::endl;
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

}  // namespace cuttlefish
