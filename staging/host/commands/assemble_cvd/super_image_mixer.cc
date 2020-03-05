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

#include "super_image_mixer.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

#include <android-base/strings.h>
#include <android-base/logging.h>

#include "common/libs/utils/archive.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace {

using cvd::FileExists;
using vsoc::DefaultHostArtifactsPath;

std::string TargetFilesZip(const cvd::FetcherConfig& fetcher_config,
                           cvd::FileSource source) {
  for (const auto& file_iter : fetcher_config.get_cvd_files()) {
    const auto& file_path = file_iter.first;
    const auto& file_info = file_iter.second;
    if (file_info.source != source) {
      continue;
    }
    std::string expected_filename = "target_files-" + file_iter.second.build_id;
    if (file_path.find(expected_filename) != std::string::npos) {
      return file_path;
    }
  }
  return "";
}

const std::string kMiscInfoPath = "META/misc_info.txt";
const std::set<std::string> kDefaultTargetImages = {
  "IMAGES/boot.img",
  "IMAGES/cache.img",
  "IMAGES/recovery.img",
  "IMAGES/userdata.img",
  "IMAGES/vendor.img",
};

bool CombineTargetZipFiles(const std::string& default_target_zip,
                           const std::string& system_target_zip,
                           const std::string& output_path) {
  cvd::Archive default_target_archive(default_target_zip);
  cvd::Archive system_target_archive(system_target_zip);

  auto default_target_contents = default_target_archive.Contents();
  if (default_target_contents.size() == 0) {
    LOG(ERROR) << "Could not open " << default_target_zip;
    return false;
  }
  auto system_target_contents = system_target_archive.Contents();
  if (system_target_contents.size() == 0) {
    LOG(ERROR) << "Could not open " << system_target_zip;
    return false;
  }
  if (mkdir(output_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
    LOG(ERROR) << "Could not create directory " << output_path;
    return false;
  }

  if (std::find(default_target_contents.begin(), default_target_contents.end(), kMiscInfoPath)
      == default_target_contents.end()) {
    LOG(ERROR) << "Default target files zip does not have " << kMiscInfoPath;
    return false;
  }
  if (!default_target_archive.ExtractFiles({kMiscInfoPath}, output_path)) {
    LOG(ERROR) << "Failed to write misc info to output directory";
    return false;
  }

  for (const auto& name : default_target_contents) {
    if (!android::base::StartsWith(name, "IMAGES/")) {
      continue;
    } else if (!android::base::EndsWith(name, ".img")) {
      continue;
    } else if (kDefaultTargetImages.count(name) == 0) {
      continue;
    }
    LOG(INFO) << "Writing " << name;
    if (!default_target_archive.ExtractFiles({name}, output_path)) {
      LOG(ERROR) << "Failed to extract " << name << " from the default target zip";
      return false;
    }
  }

  for (const auto& name : system_target_contents) {
    if (!android::base::StartsWith(name, "IMAGES/")) {
      continue;
    } else if (!android::base::EndsWith(name, ".img")) {
      continue;
    } else if (kDefaultTargetImages.count(name) > 0) {
      continue;
    }
    LOG(INFO) << "Writing " << name;
    if (!system_target_archive.ExtractFiles({name}, output_path)) {
      LOG(ERROR) << "Failed to extract " << name << " from the system target zip";
      return false;
    }
  }

  return true;
}

bool BuildSuperImage(const std::string& combined_target_zip,
                     const std::string& output_path) {
  std::string build_super_image_binary;
  std::string otatools_path;
  if (FileExists(DefaultHostArtifactsPath("otatools/bin/build_super_image"))) {
    build_super_image_binary =
        DefaultHostArtifactsPath("otatools/bin/build_super_image");
    otatools_path = DefaultHostArtifactsPath("otatools");
  } else if (FileExists(DefaultHostArtifactsPath("bin/build_super_image"))) {
    build_super_image_binary =
        DefaultHostArtifactsPath("bin/build_super_image");
    otatools_path = DefaultHostArtifactsPath("");
  } else {
    LOG(ERROR) << "Could not find otatools";
    return false;
  }
  return cvd::execute({
    build_super_image_binary,
    "--path=" + otatools_path,
    combined_target_zip,
    output_path,
  }) == 0;
}

} // namespace

bool SuperImageNeedsRebuilding(const cvd::FetcherConfig& fetcher_config,
                               const vsoc::CuttlefishConfig&) {
  bool has_default_build = false;
  bool has_system_build = false;
  for (const auto& file_iter : fetcher_config.get_cvd_files()) {
    if (file_iter.second.source == cvd::FileSource::DEFAULT_BUILD) {
      has_default_build = true;
    } else if (file_iter.second.source == cvd::FileSource::SYSTEM_BUILD) {
      has_system_build = true;
    }
  }
  return has_default_build && has_system_build;
}

bool RebuildSuperImage(const cvd::FetcherConfig& fetcher_config,
                       const vsoc::CuttlefishConfig& config,
                       const std::string& output_path) {
  std::string default_target_zip =
      TargetFilesZip(fetcher_config, cvd::FileSource::DEFAULT_BUILD);
  if (default_target_zip == "") {
    LOG(ERROR) << "Unable to find default target zip file.";
    return false;
  }
  std::string system_target_zip =
      TargetFilesZip(fetcher_config, cvd::FileSource::SYSTEM_BUILD);
  if (system_target_zip == "") {
    LOG(ERROR) << "Unable to find system target zip file.";
    return false;
  }
  auto instance = config.ForDefaultInstance();
  // TODO(schuffelen): Use cuttlefish_assembly
  std::string combined_target_path = instance.PerInstanceInternalPath("target_combined");
  // TODO(schuffelen): Use otatools/bin/merge_target_files
  if (!CombineTargetZipFiles(default_target_zip, system_target_zip,
                             combined_target_path)) {
    LOG(ERROR) << "Could not combine target zip files.";
    return false;
  }
  bool success = BuildSuperImage(combined_target_path, output_path);
  if (!success) {
    LOG(ERROR) << "Could not write the final output super image.";
  }
  return success;
}
