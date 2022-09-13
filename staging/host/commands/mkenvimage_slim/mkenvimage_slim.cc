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

// Code here has been inspired by
// https://github.com/u-boot/u-boot/blob/master/tools/mkenvimage.c The bare
// minimum amount of functionality for our application is replicated.

#include <iostream>

#include <zlib.h>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"

#define PAD_VALUE (0xff)
#define CRC_SIZE (sizeof(uint32_t))

// One NULL needed at the end of the env.
#define NULL_PAD_LENGTH (1)

DEFINE_uint32(env_size, 4096, "file size of resulting env");
DEFINE_string(output_path, "", "output file path");
DEFINE_string(input_path, "", "input file path");
namespace cuttlefish {
namespace {
std::string USAGE_MESSAGE =
    "<flags>\n"
    "\n"
    "env_size - length in bytes of the resulting env image. Defaults to 4kb.\n"
    "input_path - path to input key value mapping as a text file\n"
    "output_path - path to write resulting environment image including CRC "
    "to\n";
}  // namespace

Result<int> MkenvimageSlimMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  gflags::SetUsageMessage(USAGE_MESSAGE);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  CF_EXPECT(FLAGS_output_path != "", "Output env path isn't defined.");
  CF_EXPECT(FLAGS_env_size != 0, "env size can't be 0.");
  CF_EXPECT(!(FLAGS_env_size % 512), "env size must be multiple of 512.");

  std::string env_readout = ReadFile(FLAGS_input_path);
  CF_EXPECT(env_readout.length(), "Input env is empty");
  CF_EXPECT(
      env_readout.length() <= (FLAGS_env_size - CRC_SIZE - NULL_PAD_LENGTH),
      "Input env must fit within env_size specified.");

  std::vector<uint8_t> env_buffer(FLAGS_env_size, PAD_VALUE);
  uint8_t* env_ptr = env_buffer.data() + CRC_SIZE;
  memcpy(env_ptr, env_readout.c_str(), FileSize(FLAGS_input_path));
  env_ptr[env_readout.length()] = 0;  // final byte after the env must be NULL
  uint32_t crc = crc32(0, env_ptr, FLAGS_env_size - CRC_SIZE);
  memcpy(env_buffer.data(), &crc, sizeof(uint32_t));

  auto output_fd =
      SharedFD::Creat(FLAGS_output_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (!output_fd->IsOpen()) {
    return CF_ERR("Couldn't open the output file " + FLAGS_output_path);
  } else if (FLAGS_env_size !=
             WriteAll(output_fd, (char*)env_buffer.data(), FLAGS_env_size)) {
    RemoveFile(FLAGS_output_path);
    return CF_ERR("Couldn't complete write to " + FLAGS_output_path);
  }

  return 0;
}
}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto res = cuttlefish::MkenvimageSlimMain(argc, argv);
  if (res.ok()) {
    return *res;
  }
  LOG(ERROR) << "mkenvimage_slim failed: \n" << res.error().Message();
  LOG(ERROR) << "mkenvimage_slim failed: \n" << res.error().Trace();
  abort();
}
