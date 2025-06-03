/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/version/version.h"

#include <sstream>
#include <string>

#include <fmt/format.h>

#include "cuttlefish/host/libs/version/version.h"

namespace cuttlefish {

std::string GetVersionString() {
  std::stringstream result;
  const std::string version = GetCuttlefishCommonVersion();
  if (!version.empty()) {
    result << fmt::format("version: {}\n", version);
  }
  const std::string version_control_id = GetVcsId();
  if (!version_control_id.empty()) {
    result << fmt::format("VCS ID: {}\n", version_control_id);
  }
  return result.str();
}

}  // namespace cuttlefish
