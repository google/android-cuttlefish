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

#include <glog/logging.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/environment.h"
#include "host/libs/config/fetcher_config.h"

namespace {

/*
 * Returns the paths of all files in `directory_path`.
 *
 * This is a shallow exploration that ignores directories, i.e. it only prints
 * any regular files.
 */
std::set<std::string> ReportFiles(const std::string& directory_path) {
  // TODO(schuffelen): Put this in a common library.
  DIR* directory = opendir(directory_path.c_str());
  if (!directory) {
    int error_num = errno;
    LOG(ERROR) << "ReportFiles could not open " << directory_path << " ("
               << strerror(error_num) << ")";
    return {};
  }
  struct dirent* entry;
  std::set<std::string> found_files;
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_type == DT_DIR) {
      continue;
    }
    found_files.insert(directory_path + "/" + std::string(entry->d_name));
  }
  closedir(directory);
  return found_files;
}

/**
 * Report files that are present based on some heuristics for relevance.
 *
 * This is used in cases where it's not clear in advance whether there are
 * Cuttlefish files in the given directory.
 */
std::set<std::string> HeuristicFileReport(const std::string& directory_path) {
  std::set<std::string> files;
  if (cvd::FileExists(directory_path + "/bin/launch_cvd")) {
    files.merge(ReportFiles(directory_path + "/bin"));
  }
  bool has_super_img = cvd::FileExists(directory_path + "/super.img");
  bool has_android_info = cvd::FileExists(directory_path + "/android-info.txt");
  if (has_super_img || has_android_info) {
    files.merge(ReportFiles(directory_path));
  }
  return files;
}

} // namespace

cvd::FetcherConfig AvailableFilesReport() {
  std::string current_directory = cvd::AbsolutePath(cvd::CurrentDirectory());
  if (cvd::FileExists(current_directory + "/fetcher_config.json")) {
    cvd::FetcherConfig config;
    config.LoadFromFile(current_directory + "/fetcher_config.json");
    return config;
  }

  std::set<std::string> files;
  std::string host_out = cvd::StringFromEnv("ANDROID_HOST_OUT", "");
  if (host_out != "") {
    files.merge(ReportFiles(cvd::AbsolutePath(host_out + "/bin")));
  }

  std::string product_out = cvd::StringFromEnv("ANDROID_PRODUCT_OUT", "");
  if (product_out != "") {
    files.merge(ReportFiles(cvd::AbsolutePath(product_out)));
  }

  files.merge(HeuristicFileReport(current_directory));

  std::string home = cvd::StringFromEnv("HOME", "");
  if (home != "" && cvd::AbsolutePath(home) != current_directory) {
    files.merge(HeuristicFileReport(home));
  }

  std::string psuedo_fetcher_dir =
      cvd::StringFromEnv("ANDROID_HOST_OUT",
                         cvd::StringFromEnv("HOME", current_directory));
  std::string psuedo_fetcher_config =
      psuedo_fetcher_dir + "/launcher_psuedo_fetcher_config.json";
  files.insert(psuedo_fetcher_config);

  cvd::FetcherConfig config;
  config.RecordFlags();
  for (const auto& file : files) {
    config.add_cvd_file(cvd::CvdFile(cvd::FileSource::LOCAL_FILE, "", "", file));
  }
  config.SaveToFile(psuedo_fetcher_config);
  return config;
}
