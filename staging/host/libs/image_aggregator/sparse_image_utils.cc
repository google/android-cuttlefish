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

#include "host/libs/image_aggregator/sparse_image_utils.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <string.h>
#include <sys/file.h>

#include <fstream>
#include <string>
#include <string_view>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/config_utils.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kAndroidSparseImageMagic = "\x3A\xFF\x26\xED";

Result<SharedFD> AcquireLock(const std::string& tmp_lock_image_path) {
  SharedFD fd =
      SharedFD::Open(tmp_lock_image_path.c_str(), O_RDWR | O_CREAT, 0666);
  CF_EXPECTF(fd->IsOpen(), "Failed to open '{}': '{}'", tmp_lock_image_path,
             fd->StrError());

  CF_EXPECT(fd->Flock(LOCK_EX));

  return fd;
}

Result<bool> IsSparseImage(const std::string& image_path) {
  std::ifstream file(image_path, std::ios::binary);
  CF_EXPECTF(file.good(), "Could not open '{}'", image_path);

  std::string buffer(4, ' ');
  file.read(buffer.data(), 4);

  return buffer == kAndroidSparseImageMagic;
}

}  // namespace

Result<void> ForceRawImage(const std::string& image_path) {
  std::string tmp_lock_image_path = image_path + ".lock";

  SharedFD fd = CF_EXPECT(AcquireLock(tmp_lock_image_path));

  if (!CF_EXPECT(IsSparseImage(image_path))) {
    return {};
  }

  std::string tmp_raw_image_path = image_path + ".raw";
  // Use simg2img to convert sparse image to raw images.
  int simg2img_status =
      Execute({HostBinaryPath("simg2img"), image_path, tmp_raw_image_path});

  CF_EXPECT_EQ(simg2img_status, 0,
               "Unable to convert Android sparse image '"
                   << image_path << "' to raw image: " << simg2img_status);

  // Replace the original sparse image with the raw image.
  // `rename` can fail if these are on different mounts, but they are files
  // within the same directory so they can only be in different mounts if one
  // is a bind mount, in which case `rename` won't work anyway.
  CF_EXPECTF(rename(tmp_raw_image_path.c_str(), image_path.c_str()) == 0,
             "rename('{}','{}') failed: {}", tmp_raw_image_path, image_path,
             strerror(errno));

  return {};
}

}  // namespace cuttlefish
