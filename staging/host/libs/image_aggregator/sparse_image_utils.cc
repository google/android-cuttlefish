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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"


const char ANDROID_SPARSE_IMAGE_MAGIC[] = "\x3A\xFF\x26\xED";
namespace cuttlefish {

void ReleaseLock(const SharedFD& fd,
                 const std::string& tmp_lock_image_path) {
  auto funlock_result = fd->Flock(LOCK_UN | LOCK_NB);
  fd->Close();
  if (!funlock_result.ok()) {
    LOG(FATAL) << "It failed to unlock file " << tmp_lock_image_path;
  }
}

bool AcquireLock(SharedFD& fd, const std::string& tmp_lock_image_path) {
  fd = SharedFD::Open(tmp_lock_image_path.c_str(),
                        O_RDWR | O_CREAT, 0666);
  if (!fd->IsOpen()) {
    LOG(FATAL) << tmp_lock_image_path << " file open failed";
    return false;
  }
  auto flock_result = fd->Flock(LOCK_EX);
  if (!flock_result.ok()) {
    LOG(FATAL) << "flock failed";
    return false;
  }
  return true;
}

bool IsSparseImage(const std::string& image_path) {
  std::ifstream file(image_path, std::ios::binary);
  if (!file) {
    LOG(FATAL) << "Could not open '" << image_path << "'";
    return false;
  }
  char buffer[5] = {0};
  file.read(buffer, 4);
  file.close();
  return strcmp(ANDROID_SPARSE_IMAGE_MAGIC, buffer) == 0;
}

bool ConvertToRawImage(const std::string& image_path) {
  SharedFD fd;
  std::string tmp_lock_image_path = image_path + ".lock";

  if(AcquireLock(fd, tmp_lock_image_path) == false) {
    return false;
  }

  if (!IsSparseImage(image_path)) {
    // Release lock before return
    LOG(DEBUG) << "Skip non-sparse image " << image_path;
    return false;
  }

  auto simg2img_path = HostBinaryPath("simg2img");
  Command simg2img_cmd(simg2img_path);
  std::string tmp_raw_image_path = image_path + ".raw";
  simg2img_cmd.AddParameter(image_path);
  simg2img_cmd.AddParameter(tmp_raw_image_path);

  // Use simg2img to convert sparse image to raw image.
  int success = simg2img_cmd.Start().Wait();
  if (success != 0) {
    // Release lock before FATAL and return
    LOG(FATAL) << "Unable to convert Android sparse image " << image_path
               << " to raw image. " << success;
    return false;
  }

  // Replace the original sparse image with the raw image.
  if (unlink(image_path.c_str()) != 0) {
    // Release lock before FATAL and return
    PLOG(FATAL) << "Unable to delete original sparse image";
  }

  Command mv_cmd("/bin/mv");
  mv_cmd.AddParameter("-f");
  mv_cmd.AddParameter(tmp_raw_image_path);
  mv_cmd.AddParameter(image_path);
  success = mv_cmd.Start().Wait();
  // Release lock and leave critical section
  ReleaseLock(fd, tmp_lock_image_path);
  if (success != 0) {
    LOG(FATAL) << "Unable to rename raw image " << success;
    return false;
  }

  return true;
}

}  // namespace cuttlefish
