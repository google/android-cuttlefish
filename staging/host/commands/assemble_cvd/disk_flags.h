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

#include <chrono>
#include <memory>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/disk_builder.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

Result<void> ResolveInstanceFiles();

Result<void> CreateDynamicDiskFiles(const FetcherConfig& fetcher_config,
                                    const CuttlefishConfig& config);
Result<void> DiskImageFlagsVectorization(CuttlefishConfig& config, const FetcherConfig& fetcher_config);
std::vector<ImagePartition> GetOsCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance);
DiskBuilder OsCompositeDiskBuilder(const CuttlefishConfig& config,
                                   const CuttlefishConfig::InstanceSpecific& instance);
DiskBuilder ApCompositeDiskBuilder(const CuttlefishConfig& config,
                                   const CuttlefishConfig::InstanceSpecific& instance);

} // namespace cuttlefish
