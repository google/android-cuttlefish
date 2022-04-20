//
// Copyright (C) 2022 The Android Open Source Project
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
#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

void create_overlay_image(const CuttlefishConfig& config,
                          std::string overlay_path, bool resume);

bool ShouldCreateCompositeDisk(const std::string& composite_disk_path,
                               const std::vector<ImagePartition>& partitions);

bool DoesCompositeMatchCurrentDiskConfig(
    const std::string& prior_disk_config_path,
    const std::vector<ImagePartition>& partitions);

Result<void> CreateCompositeDiskCommon(
    const std::string& vm_manager, const std::string& header_path,
    const std::string& footer_path,
    const std::vector<ImagePartition> partitions,
    const std::string& output_path);

}  // namespace cuttlefish
