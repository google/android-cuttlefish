/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/disk/factory_reset_protected.h"

#include <string>
#include <utility>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/factory_reset_protected.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

std::string FactoryResetProtectedImage::FileName() {
  return "factory_reset_protected.img";
}

Result<FactoryResetProtectedImage> FactoryResetProtectedImage::Create(
    const CuttlefishConfig::InstanceSpecific& instance) {
  FactoryResetProtectedImage frp(instance.PerInstanceInternalPath(FileName()));
  if (FileExists(frp.path_)) {
    return frp;
  }
  CF_EXPECTF(CreateBlankImage(frp.path_, 1 /* mb */, "none"),
             "Failed to create '{}'", frp.path_);
  return frp;
}

FactoryResetProtectedImage::FactoryResetProtectedImage(std::string path)
    : path_(std::move(path)) {}

ImagePartition FactoryResetProtectedImage::Partition() const {
  return ImagePartition{
      .label = "frp",
      .image_file_path = AbsolutePath(path_),
  };
}

}  // namespace cuttlefish
