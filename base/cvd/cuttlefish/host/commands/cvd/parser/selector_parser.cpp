/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include "host/commands/cvd/parser/selector_parser.h"

#include <string>
#include <vector>

#include "host/commands/cvd/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"

namespace cuttlefish {

using cvd::config::Instance;
using cvd::config::Launch;

std::string InsName(const Instance& instance) { return instance.name(); }

std::vector<std::string> ParseSelectorConfigs(const Launch& config) {
  auto ins_name_flag = GenerateInstanceFlag("instance_name", config, InsName);

  if (!config.common().has_group_name()) {
    return {ins_name_flag};
  }

  auto group_flag = GenerateFlag("group_name", config.common().group_name());
  return {ins_name_flag, group_flag};
}

}  // namespace cuttlefish
