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

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/cache/cache.h"
#include "cuttlefish/host/commands/cvd/fetch/credential_flags.h"
#include "cuttlefish/host/libs/web/android_build_url.h"
#include "cuttlefish/host/libs/web/cas/cas_flags.h"

namespace cuttlefish {

inline constexpr char kDefaultApiKey[] = "";
inline constexpr char kDefaultCredentialSource[] = "";
inline constexpr char kDefaultProjectID[] = "";
inline constexpr std::chrono::seconds kDefaultWaitRetryPeriod =
    std::chrono::seconds(20);
inline constexpr bool kDefaultEnableCaching = true;

struct BuildApiFlags {
  std::vector<Flag> Flags();

  std::string api_key = kDefaultApiKey;
  CredentialFlags credential_flags;
  std::string credential_source = kDefaultCredentialSource;
  std::string project_id = kDefaultProjectID;
  std::chrono::seconds wait_retry_period = kDefaultWaitRetryPeriod;
  std::string api_base_url = kAndroidBuildServiceUrl;
  bool enable_caching = kDefaultEnableCaching;
  std::size_t max_cache_size_gb = kDefaultCacheSizeGb;
  CasDownloaderFlags cas_downloader_flags;
};

}  // namespace cuttlefish
