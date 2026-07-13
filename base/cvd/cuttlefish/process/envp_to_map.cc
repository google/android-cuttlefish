/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish/process/envp_to_map.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"

namespace cuttlefish {

std::unordered_map<std::string, std::string> EnvpToMap(char** envp) {
  std::unordered_map<std::string, std::string> env_map;
  if (!envp) {
    return env_map;
  }
  for (char** e = envp; *e != nullptr; e++) {
    std::vector<std::string_view> key_value =
        absl::StrSplit(*e, absl::MaxSplits('=', 1));
    if (key_value.size() != 2) {
      LOG(WARNING) << "Environment var in unknown format: " << *e;
      continue;
    }
    auto [it, inserted] = env_map.emplace(key_value[0], key_value[1]);
    if (!inserted) {
      LOG(WARNING) << "Duplicate environment variable " << key_value[0];
    }
  }
  return env_map;
}

}  // namespace cuttlefish
