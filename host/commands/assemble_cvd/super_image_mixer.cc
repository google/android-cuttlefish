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

#include <errno.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

#include <android-base/strings.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/misc_info.h"
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
  "IMAGES/odm.img",
  "IMAGES/recovery.img",
  "IMAGES/userdata.img",
  "IMAGES/vbmeta.img",
  "IMAGES/vendor.img",
};
const std::set<std::string> kDefaultTargetBuildProp = {
  "ODM/etc/build.prop",
  "VENDOR/build.prop",
};

void FindImports(cvd::Archive* archive, const std::string& build_prop_file) {
  auto contents = archive->ExtractToMemory(build_prop_file);
  auto lines = android::base::Split(contents, "\n");
  for (const auto& line : lines) {
    auto parts = android::base::Split(line, " ");
    if (parts.size() >= 2 && parts[0] == "import") {
      LOG(INFO) << build_prop_file << ": " << line;
    }
  }
}

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
  std::string output_meta = output_path + "/META";
  if (mkdir(output_meta.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
    LOG(ERROR) << "Could not create directory " << output_meta;
    return false;
  }

  if (std::find(default_target_contents.begin(), default_target_contents.end(), kMiscInfoPath)
      == default_target_contents.end()) {
    LOG(ERROR) << "Default target files zip does not have " << kMiscInfoPath;
    return false;
  }
  if (std::find(system_target_contents.begin(), system_target_contents.end(), kMiscInfoPath)
      == system_target_contents.end()) {
    LOG(ERROR) << "System target files zip does not have " << kMiscInfoPath;
    return false;
  }
  const auto default_misc =
      ParseMiscInfo(default_target_archive.ExtractToMemory(kMiscInfoPath));
  if (default_misc.size() == 0) {
    LOG(ERROR) << "Could not read the default misc_info.txt file.";
    return false;
  }
  const auto system_misc =
      ParseMiscInfo(system_target_archive.ExtractToMemory(kMiscInfoPath));
  if (system_misc.size() == 0) {
    LOG(ERROR) << "Could not read the system misc_info.txt file.";
    return false;
  }
  auto output_misc = default_misc;
  auto system_super_partitions = SuperPartitionComponents(system_misc);
  if (std::find(system_super_partitions.begin(), system_super_partitions.end(),
                "odm") == system_super_partitions.end()) {
    // odm is not one of the partitions skipped by the system check
    system_super_partitions.push_back("odm");
  }
  SetSuperPartitionComponents(system_super_partitions, &output_misc);
  auto misc_output_path = output_path + "/" + kMiscInfoPath;
  cvd::SharedFD misc_output_file =
      cvd::SharedFD::Creat(misc_output_path.c_str(), 0644);
  if (!misc_output_file->IsOpen()) {
    LOG(ERROR) << "Failed to open output misc file: "
               << misc_output_file->StrError();
    return false;
  }
  if (cvd::WriteAll(misc_output_file, WriteMiscInfo(output_misc)) < 0) {
    LOG(ERROR) << "Failed to write output misc file contents: "
               << misc_output_file->StrError();
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
  for (const auto& name : default_target_contents) {
    if (!android::base::EndsWith(name, "build.prop")) {
      continue;
    } else if (kDefaultTargetBuildProp.count(name) == 0) {
      continue;
    }
    FindImports(&default_target_archive, name);
    LOG(INFO) << "Writing " << name;
    if (!default_target_archive.ExtractFiles({name}, output_path)) {
      LOG(ERROR) << "Failed to extract " << name << " from the default target zip";
      return false;
    }
    auto name_parts = android::base::Split(name, "/");
    if (name_parts.size() < 2) {
      LOG(WARNING) << name << " does not appear to have a partition";
      continue;
    }
    auto etc_path = output_path + "/" + name_parts[0] + "/etc";
    LOG(INFO) << "Creating directory " << etc_path;
    if (mkdir(etc_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
        && errno != EEXIST) {
      PLOG(ERROR) << "Could not mkdir " << etc_path;
    }
    std::string_view name_suffix(name.data(), name.size());
    if (!android::base::ConsumePrefix(&name_suffix, name_parts[0] + "/")) {
      LOG(ERROR) << name << " did not start with " << name_parts[0] + "/";
      return false;
    }
    auto initial_path = output_path + "/" + name;
    auto dest_path = output_path + "/" + name_parts[0] + "/etc/" +
                     std::string(name_suffix);
    LOG(INFO) << "Linking " << initial_path << " to " << dest_path;
    if (link(initial_path.c_str(), dest_path.c_str())) {
      PLOG(ERROR) << "Could not link " << initial_path << " to " << dest_path;
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
  for (const auto& name : system_target_contents) {
    if (!android::base::EndsWith(name, "build.prop")) {
      continue;
    } else if (kDefaultTargetBuildProp.count(name) > 0) {
      continue;
    }
    FindImports(&system_target_archive, name);
    LOG(INFO) << "Writing " << name;
    if (!system_target_archive.ExtractFiles({name}, output_path)) {
      LOG(ERROR) << "Failed to extract " << name << " from the default target zip";
      return false;
    }
    auto name_parts = android::base::Split(name, "/");
    if (name_parts.size() < 2) {
      LOG(WARNING) << name << " does not appear to have a partition";
      continue;
    }
    auto etc_path = output_path + "/" + name_parts[0] + "/etc";
    LOG(INFO) << "Creating directory " << etc_path;
    if (mkdir(etc_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
        && errno != EEXIST) {
      PLOG(ERROR) << "Could not mkdir " << etc_path;
    }
    std::string_view name_suffix(name.data(), name.size());
    if (!android::base::ConsumePrefix(&name_suffix, name_parts[0] + "/")) {
      LOG(ERROR) << name << " did not start with " << name_parts[0] + "/";
      return false;
    }
    auto initial_path = output_path + "/" + name;
    auto dest_path = output_path + "/" + name_parts[0] + "/etc/" +
                     std::string(name_suffix);
    LOG(INFO) << "Linking " << initial_path << " to " << dest_path;
    if (link(initial_path.c_str(), dest_path.c_str())) {
      PLOG(ERROR) << "Could not link " << initial_path << " to " << dest_path;
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
