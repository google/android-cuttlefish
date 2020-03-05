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

cvd::FetcherConfig AvailableFilesReport() {
  std::string current_directory = cvd::AbsolutePath(cvd::CurrentDirectory());
  if (cvd::FileExists(current_directory + "/fetcher_config.json")) {
    cvd::FetcherConfig config;
    config.LoadFromFile(current_directory + "/fetcher_config.json");
    return config;
  }

  std::set<std::string> files;

  std::string psuedo_fetcher_dir =
      cvd::StringFromEnv("ANDROID_HOST_OUT",
                         cvd::StringFromEnv("HOME", current_directory));
  std::string psuedo_fetcher_config =
      psuedo_fetcher_dir + "/launcher_pseudo_fetcher_config.json";
  files.insert(psuedo_fetcher_config);

  cvd::FetcherConfig config;
  config.RecordFlags();
  for (const auto& file : files) {
    config.add_cvd_file(cvd::CvdFile(cvd::FileSource::LOCAL_FILE, "", "", file));
  }
  config.SaveToFile(psuedo_fetcher_config);
  return config;
}
