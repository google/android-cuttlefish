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

#include <iostream>

#include <android-base/strings.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "host/commands/assemble_cvd/flags.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {
namespace {

std::string kFetcherConfigFile = "fetcher_config.json";

FetcherConfig FindFetcherConfig(const std::vector<std::string>& files) {
  FetcherConfig fetcher_config;
  for (const auto& file : files) {
    auto expected_pos = file.size() - kFetcherConfigFile.size();
    if (file.rfind(kFetcherConfigFile) == expected_pos) {
      if (fetcher_config.LoadFromFile(file)) {
        return fetcher_config;
      }
      LOG(ERROR) << "Could not load fetcher config file.";
    }
  }
  return fetcher_config;
}

} // namespace

int AssembleCvdMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  int tty = isatty(0);
  int error_num = errno;
  CHECK_EQ(tty, 0)
      << "stdin was a tty, expected to be passed the output of a previous stage. "
      << "Did you mean to run launch_cvd?";
  CHECK(error_num != EBADF)
      << "stdin was not a valid file descriptor, expected to be passed the output "
      << "of launch_cvd. Did you mean to run launch_cvd?";

  std::string input_files_str;
  {
    auto input_fd = SharedFD::Dup(0);
    auto bytes_read = ReadAll(input_fd, &input_files_str);
    CHECK(bytes_read >= 0)
        << "Failed to read input files. Error was \"" << input_fd->StrError() << "\"";
  }
  std::vector<std::string> input_files = android::base::Split(input_files_str, "\n");

  auto config = InitFilesystemAndCreateConfig(&argc, &argv, FindFetcherConfig(input_files));

  std::cout << GetConfigFilePath(*config) << "\n";
  std::cout << std::flush;

  return 0;
}

} // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::AssembleCvdMain(argc, argv);
}
