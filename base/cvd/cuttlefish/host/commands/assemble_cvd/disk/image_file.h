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

#pragma once

#include <string>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

/**
 * An entire disk, GPT entry, or super image logical partition that ends up
 * inside a block device inside the VM.
 *
 * Image files may contain the contents of other image files, or serve as
 * indirections to other image files.
 *
 * Image files may come pre-made from an Android build, or may be generated
 * dynamically at runtime.
 *
 * Exposes the configuration of the image file as serialized data that can be
 * used to determine whether an existing image file can be used or needs to be
 * recreated.
 *
 * Instances of this class have two states: configured but not created, and
 * configured with a created image file. Subclasses should accept configuration
 * at construction time, and should not expose any additional unconfigured
 * states.
 */
class ImageFile {
 public:
  virtual ~ImageFile() = default;

  /**
   * Image name, reused in multiple places for consistency.
   *
   * - The filename (minus .img extension) used on the file system.
   * - GPT entry name
   * - Logical partition name within the super image.
   */
  virtual std::string Name() const = 0;

  /**
   * If the image file is not ready, generate it. Returns the path to the file.
   *
   * If this function succeeds, `Path()` should return the same value
   * afterwards.
   */
  virtual Result<std::string> Generate() = 0;

  /**
   * Returns the path on the filesystem where the image file is.
   *
   * The image file should be ready for use and correct for the current device
   * configuration.
   */
  virtual Result<std::string> Path() const = 0;
};

}  // namespace cuttlefish
