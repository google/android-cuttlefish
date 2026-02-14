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
#include "cuttlefish/host/libs/image_aggregator/image_from_file.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include <android-base/file.h>
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/host/libs/image_aggregator/disk_image.h"
#include "cuttlefish/host/libs/image_aggregator/qcow2.h"
#include "cuttlefish/host/libs/image_aggregator/raw.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::unique_ptr<DiskImage>> ImageFromFile(const std::string& file_path) {
  android::base::unique_fd fd(open(file_path.c_str(), O_RDONLY));
  CF_EXPECTF(fd.get() >= 0, "Could not open '{}': {}", file_path,
             StrError(errno));

  uint64_t file_size = FileSize(file_path);

  // Try to read the disk in a nicely-aligned block size unless the whole file
  // is smaller.
  constexpr uint64_t MAGIC_BLOCK_SIZE = 4096;
  std::string magic(std::min(file_size, MAGIC_BLOCK_SIZE), '\0');

  CF_EXPECTF(android::base::ReadFully(fd, magic.data(), magic.size()),
             "Failed to read '{}': {}'", file_path, StrError(errno));

  CF_EXPECTF(lseek(fd, 0, SEEK_SET) != -1, "Failed to lseek('{}'): {}",
             file_path, StrError(errno));

  // Composite disk image
  if (absl::StartsWith(magic, CompositeDiskImage::MagicString())) {
    CompositeDiskImage image =
        CF_EXPECT(CompositeDiskImage::OpenExisting(file_path));
    return std::make_unique<CompositeDiskImage>(std::move(image));
  }

  // Qcow2 image
  if (absl::StartsWith(magic, Qcow2Image::MagicString())) {
    Qcow2Image image = CF_EXPECT(Qcow2Image::OpenExisting(file_path));
    return std::make_unique<Qcow2Image>(std::move(image));
  }

  // Android-Sparse
  if (absl::StartsWith(magic, AndroidSparseImage::MagicString())) {
    AndroidSparseImage image =
        CF_EXPECT(AndroidSparseImage::OpenExisting(file_path));
    return std::make_unique<AndroidSparseImage>(std::move(image));
  }

  // raw image file
  RawImage raw = CF_EXPECT(RawImage::OpenExisting(file_path));
  return std::make_unique<RawImage>(std::move(raw));
}

}  // namespace cuttlefish
