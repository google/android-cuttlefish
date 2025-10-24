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

#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"

#include <sys/file.h>

#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <sparse/sparse.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kAndroidSparseImageMagic = "\x3A\xFF\x26\xED";

Result<SharedFD> AcquireLockForImage(const std::string& image_path) {
  std::string image_realpath;
  CF_EXPECT(android::base::Realpath(image_path, &image_realpath));
  std::string tmp_lock_image_path = image_realpath + ".lock";
  SharedFD fd =
      SharedFD::Open(tmp_lock_image_path.c_str(), O_RDWR | O_CREAT, 0666);
  CF_EXPECTF(fd->IsOpen(), "Failed to open '{}': '{}'", tmp_lock_image_path,
             fd->StrError());

  CF_EXPECT(fd->Flock(LOCK_EX));

  return fd;
}

struct SparseImageDeleter {
  void operator()(sparse_file* file) {
    sparse_file_destroy(file);
  }
};

}  // namespace

Result<bool> IsSparseImage(const std::string& image_path) {
  std::ifstream file(image_path, std::ios::binary);
  CF_EXPECTF(file.good(), "Could not open '{}'", image_path);

  std::string buffer(4, ' ');
  file.read(buffer.data(), 4);

  return buffer == kAndroidSparseImageMagic;
}

Result<void> ForceRawImage(const std::string& image_path) {
  if (!CF_EXPECT(IsSparseImage(image_path))) {
    return {};
  }
  SharedFD fd = CF_EXPECT(AcquireLockForImage(image_path));
  if (!CF_EXPECT(IsSparseImage(image_path))) {
    return {};
  }

  std::string tmp_raw_image_path = image_path + ".raw";
  // Use simg2img to convert sparse image to raw images.
  int simg2img_status =
      Execute({Simg2ImgBinary(), image_path, tmp_raw_image_path});

  CF_EXPECT_EQ(simg2img_status, 0,
               "Unable to convert Android sparse image '"
                   << image_path << "' to raw image: " << simg2img_status);

  // Replace the original sparse image with the raw image.
  // `rename` can fail if these are on different mounts, but they are files
  // within the same directory so they can only be in different mounts if one
  // is a bind mount, in which case `rename` won't work anyway.
  CF_EXPECTF(rename(tmp_raw_image_path.c_str(), image_path.c_str()) == 0,
             "rename('{}','{}') failed: {}", tmp_raw_image_path, image_path,
             StrError(errno));

  return {};
}

struct AndroidSparseImage::Impl {
  std::unique_ptr<sparse_file, SparseImageDeleter> raw_sparse_file;
  android::base::unique_fd raw_fd;
};

Result<AndroidSparseImage> AndroidSparseImage::OpenExisting(const std::string& path) {
  SharedFD fd = SharedFD::Open(path, O_RDONLY | O_CLOEXEC);
  CF_EXPECTF(fd->IsOpen(), "{}", fd->StrError());

  std::unique_ptr<Impl> impl = std::make_unique<AndroidSparseImage::Impl>();
  CF_EXPECT(impl.get());

  impl->raw_fd = android::base::unique_fd(fd->UNMANAGED_Dup());
  CF_EXPECT(impl->raw_fd.ok());

  impl->raw_sparse_file.reset(sparse_file_import(impl->raw_fd.get(), /* verbose= */ false, /* crc= */ false));
  CF_EXPECT(impl->raw_sparse_file.get());

  return AndroidSparseImage(std::move(impl));
}

AndroidSparseImage::AndroidSparseImage(std::unique_ptr<AndroidSparseImage::Impl> impl) : impl_(std::move(impl)) { }

AndroidSparseImage::AndroidSparseImage(AndroidSparseImage&& other) {
  impl_ = std::move(other.impl_);
}

AndroidSparseImage::~AndroidSparseImage() = default;

AndroidSparseImage& AndroidSparseImage::operator=(AndroidSparseImage&& other) {
  impl_ = std::move(other.impl_);
  return *this;
}

std::string AndroidSparseImage::MagicString() {
  return std::string(kAndroidSparseImageMagic);
}

Result<uint64_t> AndroidSparseImage::VirtualSizeBytes() const {
  CF_EXPECT(impl_.get());
  CF_EXPECT(impl_->raw_sparse_file.get());

  return sparse_file_len(impl_->raw_sparse_file.get(), /* sparse= */ false, /* crc= */ true);
}


}  // namespace cuttlefish
