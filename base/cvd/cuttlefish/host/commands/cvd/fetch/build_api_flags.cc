//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/cvd/fetch/build_api_flags.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <android-base/parseint.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

Flag GflagsCompatFlagSeconds(const std::string& name,
                             std::chrono::seconds& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return std::to_string(value.count()); })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        int parsed_int;
        CF_EXPECTF(android::base::ParseInt(match.value, &parsed_int),
                   "Failed to parse \"{}\" as an integer", match.value);
        value = std::chrono::seconds(parsed_int);
        return {};
      });
}

}  // namespace

std::vector<Flag> BuildApiFlags::Flags() {
  std::vector<Flag> flags;

  flags.emplace_back(GflagsCompatFlag("api_key", this->api_key)
                         .Help("API key ofr the Android Build API"));
  flags.emplace_back(
      GflagsCompatFlag("credential_source", this->credential_source)
          .Help("Build API credential source"));
  flags.emplace_back(GflagsCompatFlag("project_id", this->project_id)
                         .Help("Project ID used to access the Build API"));
  flags.emplace_back(
      GflagsCompatFlagSeconds("wait_retry_period", this->wait_retry_period)
          .Help("Retry period for pending builds given in "
                "seconds. Set to 0 to not wait."));
  flags.emplace_back(
      GflagsCompatFlag("api_base_url", this->api_base_url)
          .Help("The base url for API requests to download artifacts from"));
  flags.emplace_back(
      GflagsCompatFlag("enable_caching", this->enable_caching)
          .Help("Whether to enable local fetch file caching or not"));
  flags.emplace_back(
      GflagsCompatFlag("max_cache_size_gb", this->max_cache_size_gb)
          .Help("Max allowed size(in gigabytes) of the local fetch file cache. "
                " If the cache grows beyond this size it will be pruned after "
                "the fetches complete."));

  for (Flag flag : this->credential_flags.Flags()) {
    flags.emplace_back(std::move(flag));
  }

  for (Flag flag : this->cas_downloader_flags.Flags()) {
    flags.emplace_back(std::move(flag));
  }

  return flags;
}

}  // namespace cuttlefish
