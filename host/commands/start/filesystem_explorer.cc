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

#include "filesystem_explorer.h"

#include <set>
#include <string>

#include <dirent.h>
#include <errno.h>
#include <sys/types.h>

#include <android-base/logging.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/environment.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {

FetcherConfig AvailableFilesReport() {
  std::string current_directory = AbsolutePath(CurrentDirectory());
  FetcherConfig config;

  if (FileExists(current_directory + "/fetcher_config.json")) {
    config.LoadFromFile(current_directory + "/fetcher_config.json");
    return config;
  }

  // If needed check if `fetch_config.json` exists inside the $HOME directory.
  // `assemble_cvd` will perform a similar check.
  std::string home_directory = StringFromEnv("HOME", CurrentDirectory());
  std::string fetcher_config_path = home_directory + "/fetcher_config.json";
  if (FileExists(fetcher_config_path)) {
    config.LoadFromFile(fetcher_config_path);
  }
  return config;
}

}  // namespace cuttlefish
