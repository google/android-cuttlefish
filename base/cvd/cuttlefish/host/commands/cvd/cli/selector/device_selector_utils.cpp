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

#include "host/commands/cvd/cli/selector/device_selector_utils.h"

#include <android-base/parseint.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"

namespace cuttlefish {
namespace selector {

std::optional<std::string> OverridenHomeDirectory(const cvd_common::Envs& env) {
  Result<std::string> user_home_res = SystemWideUserHome();
  auto home_it = env.find("HOME");
  if (!user_home_res.ok() || home_it == env.end() ||
      home_it->second == *user_home_res) {
    return std::nullopt;
  }
  return home_it->second;
}

}  // namespace selector
}  // namespace cuttlefish
