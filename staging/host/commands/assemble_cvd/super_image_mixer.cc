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
#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/misc_info.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {
namespace {

std::string TargetFilesZip(const FetcherConfig& fetcher_config,
                           FileSource source) {
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
    "IMAGES/boot.img",        "IMAGES/init_boot.img", "IMAGES/odm.img",
    "IMAGES/odm_dlkm.img",    "IMAGES/recovery.img",  "IMAGES/userdata.img",
    "IMAGES/vbmeta.img",      "IMAGES/vendor.img",    "IMAGES/vendor_dlkm.img",
    "IMAGES/system_dlkm.img",
};
const std::set<std::string> kDefaultTargetBuildProp = {
  "ODM/build.prop",
  "ODM/etc/build.prop",
  "VENDOR/build.prop",
  "VENDOR/etc/build.prop",
};

void FindImports(Archive* archive, const std::string& build_prop_file) {
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
  Archive default_target_archive(default_target_zip);
  Archive system_target_archive(system_target_zip);

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
  // Ensure specific skipped partitions end up in the misc_info.txt
  for (auto partition :
       {"odm", "odm_dlkm", "vendor", "vendor_dlkm", "system_dlkm"}) {
    if (std::find(system_super_partitions.begin(), system_super_partitions.end(),
                  partition) == system_super_partitions.end()) {
      system_super_partitions.push_back(partition);
    }
  }
  if (!SetSuperPartitionComponents(system_super_partitions, &output_misc)) {
    LOG(ERROR) << "Failed to update super partitions components for misc_info";
    return false;
  }

  auto misc_output_path = output_path + "/" + kMiscInfoPath;
  SharedFD misc_output_file =
      SharedFD::Creat(misc_output_path.c_str(), 0644);
  if (!misc_output_file->IsOpen()) {
    LOG(ERROR) << "Failed to open output misc file: "
               << misc_output_file->StrError();
    return false;
  }
  if (WriteAll(misc_output_file, WriteMiscInfo(output_misc)) < 0) {
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
  } else if (FileExists(HostBinaryPath("build_super_image"))) {
    build_super_image_binary =
        HostBinaryPath("build_super_image");
    otatools_path = DefaultHostArtifactsPath("");
  } else {
    LOG(ERROR) << "Could not find otatools";
    return false;
  }
  return execute({
    build_super_image_binary,
    "--path=" + otatools_path,
    combined_target_zip,
    output_path,
  }) == 0;
}

bool SuperImageNeedsRebuilding(const FetcherConfig& fetcher_config) {
  bool has_default_build = false;
  bool has_system_build = false;
  for (const auto& file_iter : fetcher_config.get_cvd_files()) {
    if (file_iter.second.source == FileSource::DEFAULT_BUILD) {
      has_default_build = true;
    } else if (file_iter.second.source == FileSource::SYSTEM_BUILD) {
      has_system_build = true;
    }
  }
  return has_default_build && has_system_build;
}

bool RebuildSuperImage(const FetcherConfig& fetcher_config,
                       const CuttlefishConfig& config,
                       const std::string& output_path) {
  std::string default_target_zip =
      TargetFilesZip(fetcher_config, FileSource::DEFAULT_BUILD);
  if (default_target_zip == "") {
    LOG(ERROR) << "Unable to find default target zip file.";
    return false;
  }
  std::string system_target_zip =
      TargetFilesZip(fetcher_config, FileSource::SYSTEM_BUILD);
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

class SuperImageOutputPathTag {};

class SuperImageRebuilderImpl : public SuperImageRebuilder {
 public:
  INJECT(SuperImageRebuilderImpl(const FetcherConfig& fetcher_config,
                                 const CuttlefishConfig& config,
                                 ANNOTATED(SuperImageOutputPathTag, std::string)
                                     output_path))
      : fetcher_config_(fetcher_config),
        config_(config),
        output_path_(output_path) {}

  std::string Name() const override { return "SuperImageRebuilderImpl"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override {
    if (SuperImageNeedsRebuilding(fetcher_config_)) {
      bool success = RebuildSuperImage(fetcher_config_, config_, output_path_);
      if (!success) {
        LOG(ERROR)
            << "Super image rebuilding requested but could not be completed.";
        return false;
      }
    }
    return true;
  }

  const FetcherConfig& fetcher_config_;
  const CuttlefishConfig& config_;
  std::string output_path_;
};

}  // namespace

fruit::Component<fruit::Required<const FetcherConfig, const CuttlefishConfig>,
                 SuperImageRebuilder>
SuperImageRebuilderComponent(const std::string* output_path) {
  return fruit::createComponent()
      .bindInstance<fruit::Annotated<SuperImageOutputPathTag, std::string>>(
          *output_path)
      .bind<SuperImageRebuilder, SuperImageRebuilderImpl>()
      .addMultibinding<SetupFeature, SuperImageRebuilder>();
}

} // namespace cuttlefish
