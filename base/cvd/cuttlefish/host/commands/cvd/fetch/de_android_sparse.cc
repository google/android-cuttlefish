//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/cvd/fetch/de_android_sparse.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <sparse/sparse.h>
#include "absl/log/log.h"

#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

#ifndef O_BINARY
#define O_BINARY 0
#endif

bool ConvertToRawImageNoBinary(const std::string& image_path) {
  std::string tmp_raw_image_path = image_path + ".raw";

  // simg2img logic to convert sparse image to raw image.
  struct sparse_file* s;
  int out = open(tmp_raw_image_path.c_str(),
                 O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0664);
  int in = open(image_path.c_str(), O_RDONLY | O_BINARY);
  if (in < 0) {
    LOG(FATAL) << "Cannot open input file " << image_path;
    return false;
  }

  s = sparse_file_import(in, true, false);
  if (!s) {
    LOG(FATAL) << "Failed to read sparse file " << image_path;
    return false;
  }

  if (lseek(out, 0, SEEK_SET) == -1) {
    LOG(FATAL) << "lseek failed " << tmp_raw_image_path;
    return false;
  }

  if (sparse_file_write(s, out, false, false, false) < 0) {
    LOG(FATAL) << "Cannot write output file " << image_path;
    return false;
  }
  sparse_file_destroy(s);
  close(in);
  close(out);

  // Replace the original sparse image with the raw image.
  if (unlink(image_path.c_str()) != 0) {
    PLOG(FATAL) << "Unable to delete original sparse image";
  }

  int success = rename(tmp_raw_image_path.c_str(), image_path.c_str());
  if (success != 0) {
    LOG(FATAL) << "Unable to rename raw image " << success;
    return false;
  }

  return true;
}

}  // namespace

Result<void> DeAndroidSparse2(const std::vector<std::string>& image_files) {
  for (const auto& file : image_files) {
    if (!CF_EXPECT(IsSparseImage(file))) {
      continue;
    }
    if (ConvertToRawImageNoBinary(file)) {
      VLOG(0) << "De-sparsed '" << file << "'";
    } else {
      LOG(ERROR) << "Failed to de-sparse '" << file << "'";
    }
  }
  return {};
}

}  // namespace cuttlefish
