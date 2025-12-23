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

#include <string>

#include "cuttlefish/host/libs/image_aggregator/disk_image.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> ForceRawImage(const std::string& image_path);
Result<bool> IsSparseImage(const std::string& image_path);

/** Image file format comprised of a list of chunks of "raw data" and "fill
 * data" that is a repeated byte string.  */
class AndroidSparseImage : public DiskImage {
 public:
  static Result<AndroidSparseImage> OpenExisting(const std::string& path);

  AndroidSparseImage(AndroidSparseImage&&);
  ~AndroidSparseImage() override;
  AndroidSparseImage& operator=(AndroidSparseImage&&);

  /** "Sparse header magic", used to identify the file type.
   *
   * Valid android-sparse files start with this prefix.
   *
   * https://android.googlesource.com/platform/system/core/+/7b444f08c17ed1b82ea1a1560e109c0a173e700f/libsparse/sparse_format.h#39
   */
  static std::string MagicString();

  Result<uint64_t> VirtualSizeBytes() const override;

 private:
  struct Impl;

  AndroidSparseImage(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

}  // namespace cuttlefish
