//
// Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/esp/make_fat_image.h"

#include <sys/types.h>

#include <fcntl.h>

#include <string>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> MakeFatImage(const std::string& data_image, int data_image_mb,
                          int offset_num_mb) {
  off_t offset_size_bytes = static_cast<off_t>(offset_num_mb) << 20;
  off_t image_size_bytes = static_cast<off_t>(data_image_mb) << 20;

  if (FileExists(MkfsFat())) {
    auto fd = SharedFD::Open(data_image, O_CREAT | O_TRUNC | O_RDWR, 0666);
    CF_EXPECTF(fd->Truncate(image_size_bytes) == 0,
               "`truncate --size={}M '{}'` failed: {}", data_image_mb,
               data_image, fd->StrError());

    CF_EXPECT_EQ(
        Execute({MkfsFat(), "-F", "32", "-M", "0xf8", "-h", "0", "-s", "8",
                 "-g", "255/63", "-S", "512",
                 "--offset=" + std::to_string(offset_size_bytes), data_image}),
        0);
  } else {
    image_size_bytes -= offset_size_bytes;
    off_t image_size_sectors = image_size_bytes / 512;

    CF_EXPECT_EQ(Execute({NewfsMsdos(),
                          "-F",
                          "32",
                          "-m",
                          "0xf8",
                          "-o",
                          "0",
                          "-c",
                          "8",
                          "-h",
                          "255",
                          "-u",
                          "63",
                          "-S",
                          "512",
                          "-s",
                          std::to_string(image_size_sectors),
                          "-C",
                          std::to_string(data_image_mb) + "M",
                          "-@",
                          std::to_string(offset_size_bytes),
                          data_image}),
                 0);
  }

  return {};
}

}  // namespace cuttlefish
