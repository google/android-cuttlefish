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

#include <stdlib.h>

#include "absl/log/log.h"
#include "gflags/gflags.h"

#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/libs/config/mkenvimage_slim.h"
#include "cuttlefish/result/result.h"

DEFINE_int32(env_size, 4096, "file size of resulting env");
DEFINE_string(output_path, "", "output file path");
DEFINE_string(input_path, "", "input file path");

namespace cuttlefish {

static constexpr char kUsageMessage[] =
    "<flags>\n"
    "\n"
    "env_size - length in bytes of the resulting env image. Defaults to 4kb.\n"
    "input_path - path to input key value mapping as a text file\n"
    "output_path - path to write resulting environment image including CRC "
    "to\n";

Result<void> MkenvimageSlimMain(int argc, char** argv) {
  cuttlefish::LogToStderr();
  gflags::SetUsageMessage(kUsageMessage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  CF_EXPECT(!FLAGS_output_path.empty(), "Output env path isn't defined.");
  CF_EXPECT(FLAGS_env_size != 0, "env size can't be 0.");
  CF_EXPECT(!(FLAGS_env_size % 512), "env size must be multiple of 512.");

  CF_EXPECT(
      MkenvimageSlim(FLAGS_input_path, FLAGS_output_path, FLAGS_env_size));
  return {};
}
}  // namespace cuttlefish

int main(int argc, char** argv) {
  if (cuttlefish::Result<void> res = cuttlefish::MkenvimageSlimMain(argc, argv);
      res.ok()) {
    return 0;
  } else {
    LOG(ERROR) << "mkenvimage_slim failed: \n" << res.error();
    return 1;
  }
}
