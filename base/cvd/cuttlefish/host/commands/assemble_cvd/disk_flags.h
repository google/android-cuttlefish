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

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk_builder.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"

namespace cuttlefish {

Result<void> ResolveInstanceFiles(const SystemImageDirFlag& system_image_dir);

Result<void> CreateDynamicDiskFiles(const FetcherConfig& fetcher_config,
                                    const CuttlefishConfig& config,
                                    const SystemImageDirFlag&);
Result<void> DiskImageFlagsVectorization(CuttlefishConfig& config,
                                         const FetcherConfig& fetcher_config,
                                         const SystemImageDirFlag&);
DiskBuilder OsCompositeDiskBuilder(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance, const MetadataImage&,
    const MiscImage&, const SystemImageDirFlag&);
DiskBuilder ApCompositeDiskBuilder(const CuttlefishConfig& config,
                                   const CuttlefishConfig::InstanceSpecific& instance);

} // namespace cuttlefish
