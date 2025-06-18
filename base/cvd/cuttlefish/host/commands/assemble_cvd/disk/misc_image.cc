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

#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"

#include <android-base/logging.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

Result<MiscImage> MiscImage::Reuse(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string path = instance.PerInstancePath(Name());
  CF_EXPECT(FileHasContent(path));

  LOG(DEBUG) << "misc partition image already exists";

  return MiscImage(path);
}

Result<MiscImage> MiscImage::ReuseOrCreate(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string path = instance.PerInstancePath(Name());

  LOG(DEBUG) << "misc partition image: creating empty at '" << path << "'";
  CF_EXPECT(CreateBlankImage(path, 1 /* mb */, "none"),
            "Failed to create misc image");
  return MiscImage(path);
}

MiscImage::MiscImage(std::string path) : path_(std::move(path)) {}

std::string MiscImage::Name() { return "misc.img"; }

ImagePartition MiscImage::Partition() const {
  return ImagePartition{
      .label = "misc",
      .image_file_path = path_,
  };
}

}  // namespace cuttlefish
