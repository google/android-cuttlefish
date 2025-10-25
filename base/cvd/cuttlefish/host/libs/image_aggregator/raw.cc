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

#include "cuttlefish/host/libs/image_aggregator/raw.h"

#include <string>
#include <utility>

#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/files.h"

namespace cuttlefish {

Result<RawImage> RawImage::OpenExisting(const std::string& path) {
  off_t size = FileSize(path);
  CF_EXPECT_GE(size, 0, StrError(errno));

  return RawImage(size);
}

RawImage::RawImage(RawImage&& other) { size_ = std::move(other.size_); }
RawImage::~RawImage() = default;
RawImage& RawImage::operator=(RawImage&& other) {
  size_ = std::move(other.size_);
  return *this;
}

Result<uint64_t> RawImage::VirtualSizeBytes() const { return size_; }

RawImage::RawImage(uint64_t size) : size_(size) {}

}  // namespace cuttlefish
