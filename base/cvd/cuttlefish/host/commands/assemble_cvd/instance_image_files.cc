//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/instance_image_files.h"

#include <memory>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/disk/image_file.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

std::vector<std::vector<std::unique_ptr<ImageFile>>> InstanceImageFiles(
    const CuttlefishConfig& config) {
  std::vector<std::vector<std::unique_ptr<ImageFile>>> image_files;

  for (const auto& instance : config.Instances()) {
    std::vector<std::unique_ptr<ImageFile>>& instance_image_files =
        image_files.emplace_back();

    instance_image_files.emplace_back(
        std::make_unique<MetadataImage>(instance));
    instance_image_files.emplace_back(std::make_unique<MiscImage>(instance));
  }

  return image_files;
}

}  // namespace cuttlefish
