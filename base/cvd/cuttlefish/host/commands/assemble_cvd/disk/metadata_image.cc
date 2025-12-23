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

#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"

#include <stddef.h>

#include <string>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

static constexpr size_t kMetadataImageMb = 64;
static constexpr size_t kMetadataImageBytes = kMetadataImageMb << 20;

Result<MetadataImage> MetadataImage::ReuseOrCreate(
    const CuttlefishConfig::InstanceSpecific& instance) {
  Result<MetadataImage> reused = Reuse(instance);
  if (reused.ok()) {
    return reused;
  }

  std::string path = instance.PerInstancePath(Name());

  CF_EXPECTF(CreateBlankImage(path, kMetadataImageMb, "none"),
             "Failed to create '{}' with size '{}' MB", path, kMetadataImageMb);

  return MetadataImage(std::move(path));
}

Result<MetadataImage> MetadataImage::Reuse(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string path = instance.PerInstancePath(Name());

  CF_EXPECT(FileExists(path));
  CF_EXPECT_EQ(FileSize(path), kMetadataImageBytes);

  return MetadataImage(std::move(path));
}

MetadataImage::MetadataImage(std::string path) : path_(std::move(path)) {}

std::string MetadataImage::Name() { return "metadata.img"; }

ImagePartition MetadataImage::Partition() const {
  return ImagePartition{
      .label = "metadata",
      .image_file_path = path_,
  };
}

}  // namespace cuttlefish
