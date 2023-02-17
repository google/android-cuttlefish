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

#include <string.h>

#include <fstream>

#include <android-base/logging.h>

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

const char ANDROID_SPARSE_IMAGE_MAGIC[] = "\x3A\xFF\x26\xED";
namespace cuttlefish {

bool IsSparseImage(const std::string& image_path) {
  std::ifstream file(image_path, std::ios::binary);
  if (!file) {
    LOG(FATAL) << "Could not open " << image_path;
    return false;
  }
  char buffer[5] = {0};
  file.read(buffer, 4);
  file.close();
  return strcmp(ANDROID_SPARSE_IMAGE_MAGIC, buffer) == 0;
}

bool ConvertToRawImage(const std::string& image_path) {
  if (!IsSparseImage(image_path)) {
    LOG(INFO) << "Skip non-sparse image " << image_path;
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
    LOG(FATAL) << "Unable to convert Android sparse image " << image_path
               << " to raw image. " << success;
    return false;
  }

  // Replace the original sparse image with the raw image.
  Command mv_cmd("/usr/bin/mv");
  mv_cmd.AddParameter("-f");
  mv_cmd.AddParameter(tmp_raw_image_path);
  mv_cmd.AddParameter(image_path);
  success = mv_cmd.Start().Wait();
  if (success != 0) {
    LOG(FATAL) << "Unable to replace original sparse image " << success;
    return false;
  }

  return true;
}

}  // namespace cuttlefish
