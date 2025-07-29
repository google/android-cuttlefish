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

#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"

#include <string>
#include <utility>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/image_aggregator/cdisk_spec.pb.h"

namespace cuttlefish {

struct CompositeDiskImage::Impl {
  CompositeDisk cdisk;
};

Result<CompositeDiskImage> CompositeDiskImage::OpenExisting(
    const std::string& path) {
  SharedFD fd = SharedFD::Open(path, O_CLOEXEC, O_RDONLY);
  CF_EXPECT(fd->IsOpen(), fd->StrError());

  std::string magic = MagicString();
  CF_EXPECT_EQ(ReadExact(fd, &magic), magic.size(), fd->StrError());
  CF_EXPECT_EQ(magic, MagicString());

  std::string message;
  CF_EXPECTF(ReadAll(fd, &message) >= 0, "{}", fd->StrError());

  std::unique_ptr<Impl> impl(new Impl());
  CF_EXPECT(impl.get());

  CF_EXPECTF(impl->cdisk.ParseFromString(message), "Failed to parse '{}': {}",
             path, strerror(errno));

  return CompositeDiskImage(std::move(impl));
}

std::string CompositeDiskImage::MagicString() { return "composite_disk\x1d"; }

CompositeDiskImage::CompositeDiskImage(CompositeDiskImage&& other) {
  impl_ = std::move(other.impl_);
}
CompositeDiskImage::~CompositeDiskImage() = default;
CompositeDiskImage& CompositeDiskImage::operator=(CompositeDiskImage&& other) {
  impl_ = std::move(other.impl_);
  return *this;
}

Result<uint64_t> CompositeDiskImage::VirtualSizeBytes() const {
  CF_EXPECT(impl_.get());

  return impl_->cdisk.length();
}

CompositeDiskImage::CompositeDiskImage(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

}  // namespace cuttlefish
