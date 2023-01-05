/*
 * Copyright (C) 2019 The Android Open Source Project
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
#pragma once

#include <fruit/fruit.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {

struct GuestConfig {
  Arch target_arch;
  bool bootconfig_supported;
  bool hctr2_supported;
  std::string android_version_number;
};

Result<std::vector<GuestConfig>> GetGuestConfigAndSetDefaults();
// Must be called after ParseCommandLineFlags.
Result<CuttlefishConfig> InitializeCuttlefishConfiguration(
    const std::string& root_dir,
    const std::vector<GuestConfig>& guest_configs,
    fruit::Injector<>& injector, const FetcherConfig& fetcher_config);

std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();
std::string GetSeccompPolicyDir();

} // namespace cuttlefish
