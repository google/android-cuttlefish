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

#include <stdint.h>

#include <memory>
#include <string>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/image_aggregator/disk_image.h"

namespace cuttlefish {

Result<std::unique_ptr<DiskImage>> ImageFromFile(const std::string& path);

/** A file where the raw bytes are presented as a disk to a virtual machine. */
class RawImage : public DiskImage {
 public:
  RawImage(RawImage&&);
  ~RawImage() override;
  RawImage& operator=(RawImage&&);

  Result<uint64_t> VirtualSizeBytes() const override;

 private:
  static Result<RawImage> OpenExisting(const std::string& path);

  RawImage(uint64_t size);

  friend Result<std::unique_ptr<DiskImage>> ImageFromFile(
      const std::string& path);

  uint64_t size_;
};

}  // namespace cuttlefish
