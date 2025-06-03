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

#include <string>

#include <fmt/format.h>

#include "cuttlefish/host/libs/version/version.h"

namespace cuttlefish {

VersionIdentifiers GetVersionIds() {
  return VersionIdentifiers{
      .package = GetCuttlefishCommonVersion(),
      .version_control = GetVcsId(),
  };
}

std::string VersionIdentifiers::ToString() const {
  const auto version_ids = GetVersionIds();
  return fmt::format("version: {} | VCS: {}", version_ids.package,
                     version_ids.version_control);
}

std::string VersionIdentifiers::ToPrettyString() const {
  const auto version_ids = GetVersionIds();
  return fmt::format("version: {}\nVCS: {}\n", version_ids.package,
                     version_ids.version_control);
}

}  // namespace cuttlefish
