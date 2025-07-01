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

#include <string>

#include <fruit/fruit.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"

namespace cuttlefish {

Result<std::vector<GuestConfig>> GetGuestConfigAndSetDefaults(
    const SystemImageDirFlag&);
// Must be called after ParseCommandLineFlags.
Result<CuttlefishConfig> InitializeCuttlefishConfiguration(
    const std::string& root_dir, const std::vector<GuestConfig>& guest_configs,
    fruit::Injector<>& injector, const FetcherConfig& fetcher_config,
    const SystemImageDirFlag&);

std::string GetConfigFilePath(const CuttlefishConfig& config);

} // namespace cuttlefish
