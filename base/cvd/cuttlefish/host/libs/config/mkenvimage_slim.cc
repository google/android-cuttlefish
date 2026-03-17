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
#include "cuttlefish/host/libs/config/mkenvimage_slim.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include <zlib.h>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

#define PAD_VALUE (0xff)
#define CRC_SIZE (sizeof(uint32_t))

// One NULL needed at the end of the env.
#define NULL_PAD_LENGTH (1)

Result<void> MkenvimageSlim(const std::string& input_path,
                            const std::string& output_path, size_t env_size) {
  std::string env_readout = ReadFile(std::string(input_path));
  CF_EXPECT(env_readout.length(), "Input env is empty");
  CF_EXPECT(env_readout.length() <= (env_size - CRC_SIZE - NULL_PAD_LENGTH),
            "Input env must fit within env_size specified.");

  std::vector<uint8_t> env_buffer(env_size, PAD_VALUE);
  uint8_t* env_ptr = env_buffer.data() + CRC_SIZE;
  memcpy(env_ptr, env_readout.c_str(), FileSize(std::string(input_path)));
  env_ptr[env_readout.length()] = 0;  // final byte after the env must be NULL
  uint32_t crc = crc32(0, env_ptr, env_size - CRC_SIZE);
  memcpy(env_buffer.data(), &crc, sizeof(uint32_t));

  // NOLINTBEGIN(misc-include-cleaner)
  auto output_fd = SharedFD::Creat(std::string(output_path),
                                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  // NOLINTEND(misc-include-cleaner)
  if (!output_fd->IsOpen()) {
    return CF_ERR("Couldn't open the output file " + output_path);
  } else if (env_size !=
             WriteAll(output_fd, (char*)env_buffer.data(), env_size)) {
    if (Result<void> res = RemoveFile(output_path); !res.ok()) {
      LOG(ERROR) << res.error();
    }
    return CF_ERR("Couldn't complete write to " + output_path);
  }
  return {};
}

}  // namespace cuttlefish
