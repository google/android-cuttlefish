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

/** File representing a virtual disk made of separate component files.  */
class CompositeDiskImage : public DiskImage {
 public:
  static Result<CompositeDiskImage> OpenExisting(const std::string& path);

  CompositeDiskImage(CompositeDiskImage&&);
  ~CompositeDiskImage() override;
  CompositeDiskImage& operator=(CompositeDiskImage&&);

  /** "Composite disk magic string", used to identify the file type.
   *
   * Valid composite disk files start with this prefix.
   *
   * https://chromium.googlesource.com/crosvm/crosvm/+/2e16335044c8e54249ed2434b6a01fe827738570/disk/src/composite.rs#168
   */
  static std::string MagicString();

  Result<uint64_t> VirtualSizeBytes() const override;

 private:
  struct Impl;

  CompositeDiskImage(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

}  // namespace cuttlefish
