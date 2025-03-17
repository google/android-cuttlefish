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

#include "host/commands/assemble_cvd/super_image_mixer.h"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>

#include "common/libs/utils/archive.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/misc_info.h"
#include "host/libs/avb/avb.h"
#include "host/libs/config/config_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

constexpr char kMiscInfoPath[] = "META/misc_info.txt";
constexpr char kDynamicPartitionsPath[] = "META/dynamic_partitions_info.txt";
constexpr std::array kVendorTargetImages = {
    "IMAGES/boot.img",          "IMAGES/dtbo.img",
    "IMAGES/init_boot.img",     "IMAGES/odm.img",
    "IMAGES/odm_dlkm.img",      "IMAGES/recovery.img",
    "IMAGES/system_dlkm.img",   "IMAGES/userdata.img",
    "IMAGES/vbmeta.img",        "IMAGES/vbmeta_system_dlkm.img",
    "IMAGES/vbmeta_vendor.img", "IMAGES/vbmeta_vendor_dlkm.img",
    "IMAGES/vendor.img",        "IMAGES/vendor_boot.img",
    "IMAGES/vendor_dlkm.img",   "IMAGES/vendor_kernel_boot.img",
};
constexpr std::array kVendorTargetBuildProps = {
    "ODM/build.prop",
    "ODM/etc/build.prop",
    "VENDOR/build.prop",
    "VENDOR/etc/build.prop",
};

struct RebuildPaths {
  std::string vendor_target_zip;
  std::string system_target_zip;
  std::string combined_target_zip;
  std::string super_image_output;
  std::string vbmeta_image_output;
};

struct TargetFiles {
  Archive vendor_zip;
  Archive system_zip;
  std::vector<std::string> vendor_contents;
  std::vector<std::string> system_contents;
};

struct Extracted {
  std::set<std::string> images;
  std::vector<std::string> system_partitions;
};

void FindImports(Archive* archive, const std::string& build_prop_file) {
  auto contents = archive->ExtractToMemory(build_prop_file);
  auto lines = android::base::Split(contents, "\n");
  for (const auto& line : lines) {
    auto parts = android::base::Split(line, " ");
    if (parts.size() >= 2 && parts[0] == "import") {
      LOG(DEBUG) << build_prop_file << ": " << line;
    }
  }
}

bool IsTargetFilesImage(const std::string& filename) {
  return android::base::StartsWith(filename, "IMAGES/") &&
         android::base::EndsWith(filename, ".img");
}

bool IsTargetFilesBuildProp(const std::string& filename) {
  return android::base::EndsWith(filename, "build.prop");
}

Result<std::string> GetPartitionNameFromPath(const std::string& path) {
  std::string_view result(path);
  CF_EXPECTF(
      android::base::ConsumePrefix(&result, "IMAGES/"),
      "target_files filepath {} expected to be in the \"IMAGES\" directory",
      path);
  CF_EXPECTF(android::base::ConsumeSuffix(&result, ".img"),
             "target_files filepath {} expected to be a \".img\" file", path);
  return std::string(result);
}

Result<TargetFiles> GetTargetFiles(const std::string& vendor_zip_path,
                                   const std::string& system_zip_path) {
  auto result = TargetFiles{
      .vendor_zip = Archive(vendor_zip_path),
      .system_zip = Archive(system_zip_path),
  };
  result.vendor_contents = result.vendor_zip.Contents();
  result.system_contents = result.system_zip.Contents();
  CF_EXPECTF(!result.vendor_contents.empty(), "Could not open {}",
             vendor_zip_path);
  CF_EXPECTF(!result.system_contents.empty(), "Could not open {}",
             system_zip_path);
  return result;
}

Result<MiscInfo> CombineDynamicPartitionsInfo(
    TargetFiles& target_files, const std::set<std::string>& extracted_images) {
  CF_EXPECTF(Contains(target_files.vendor_contents, kDynamicPartitionsPath),
             "Vendor target files zip does not contain {}",
             kDynamicPartitionsPath);
  CF_EXPECTF(Contains(target_files.system_contents, kDynamicPartitionsPath),
             "System target files zip does not contain {}",
             kDynamicPartitionsPath);

  const MiscInfo vendor_dp_info = CF_EXPECT(ParseMiscInfo(
      target_files.vendor_zip.ExtractToMemory(kDynamicPartitionsPath)));
  const MiscInfo system_dp_info = CF_EXPECT(ParseMiscInfo(
      target_files.system_zip.ExtractToMemory(kDynamicPartitionsPath)));

  return CF_EXPECT(GetCombinedDynamicPartitions(vendor_dp_info, system_dp_info,
                                                extracted_images));
}

Result<MiscInfo> CombineMiscInfo(
    TargetFiles& target_files, const std::string& misc_output_path,
    const std::set<std::string>& extracted_images,
    const std::vector<std::string>& system_partitions) {
  CF_EXPECTF(Contains(target_files.vendor_contents, kMiscInfoPath),
             "Vendor target files zip does not contain {}", kMiscInfoPath);
  CF_EXPECTF(Contains(target_files.system_contents, kMiscInfoPath),
             "System target files zip does not contain {}", kMiscInfoPath);

  const MiscInfo vendor_misc = CF_EXPECT(
      ParseMiscInfo(target_files.vendor_zip.ExtractToMemory(kMiscInfoPath)));
  const MiscInfo system_misc = CF_EXPECT(
      ParseMiscInfo(target_files.system_zip.ExtractToMemory(kMiscInfoPath)));

  const auto combined_dp_info =
      CF_EXPECT(CombineDynamicPartitionsInfo(target_files, extracted_images));
  const auto output_misc = CF_EXPECT(MergeMiscInfos(
      vendor_misc, system_misc, combined_dp_info, system_partitions));

  CF_EXPECT(WriteMiscInfo(output_misc, misc_output_path));
  return std::move(output_misc);
}

Result<Extracted> ExtractTargetFiles(TargetFiles& target_files,
                                     const std::string& combined_output_path) {
  Extracted extracted;
  for (const auto& name : target_files.vendor_contents) {
    if (!IsTargetFilesImage(name)) {
      continue;
    } else if (!Contains(kVendorTargetImages, name)) {
      continue;
    }
    LOG(DEBUG) << "Writing " << name << " from vendor target";
    CF_EXPECT(
        target_files.vendor_zip.ExtractFiles({name}, combined_output_path),
        "Failed to extract " << name << " from the vendor target zip");
    extracted.images.emplace(CF_EXPECT(GetPartitionNameFromPath(name)));
  }
  for (const auto& name : target_files.vendor_contents) {
    if (!IsTargetFilesBuildProp(name)) {
      continue;
    } else if (!Contains(kVendorTargetBuildProps, name)) {
      continue;
    }
    FindImports(&target_files.vendor_zip, name);
    LOG(DEBUG) << "Writing " << name << " from vendor target";
    CF_EXPECT(
        target_files.vendor_zip.ExtractFiles({name}, combined_output_path),
        "Failed to extract " << name << " from the vendor target zip");
  }
  LOG(INFO) << "Completed extracting images from vendor.";

  for (const auto& name : target_files.system_contents) {
    if (!IsTargetFilesImage(name)) {
      continue;
    } else if (Contains(kVendorTargetImages, name)) {
      continue;
    }
    LOG(DEBUG) << "Writing " << name << " from system target";
    CF_EXPECT(
        target_files.system_zip.ExtractFiles({name}, combined_output_path),
        "Failed to extract " << name << " from the system target zip");
    const auto partition = CF_EXPECT(GetPartitionNameFromPath(name));
    extracted.images.emplace(partition);
    extracted.system_partitions.emplace_back(partition);
  }
  for (const auto& name : target_files.system_contents) {
    if (!IsTargetFilesBuildProp(name)) {
      continue;
    } else if (Contains(kVendorTargetBuildProps, name)) {
      continue;
    }
    FindImports(&target_files.system_zip, name);
    LOG(DEBUG) << "Writing " << name << " from system target";
    CF_EXPECT(
        target_files.system_zip.ExtractFiles({name}, combined_output_path),
        "Failed to extract " << name << " from the system target zip");
  }
  LOG(INFO) << "Completed extracting images from system.";
  return extracted;
}

Result<void> RegenerateVbmeta(const MiscInfo& misc_info,
                              const std::string& output_path,
                              const std::string& image_path) {
  const VbmetaArgs args = CF_EXPECT(GetVbmetaArgs(misc_info, image_path));
  auto avbtool = Avb(AvbToolBinary(), args.algorithm, args.key_path);
  CF_EXPECT(avbtool.MakeVbMetaImage(output_path, args.chained_partitions,
                                    args.included_partitions,
                                    args.extra_arguments));
  return {};
}

Result<void> CombineTargetZipFiles(const RebuildPaths& paths) {
  CF_EXPECT(EnsureDirectoryExists(paths.combined_target_zip));
  CF_EXPECT(EnsureDirectoryExists(paths.combined_target_zip + "/META"));
  auto target_files = CF_EXPECT(
      GetTargetFiles(paths.vendor_target_zip, paths.system_target_zip));
  const auto extracted =
      CF_EXPECT(ExtractTargetFiles(target_files, paths.combined_target_zip));
  const auto misc_output_path = paths.combined_target_zip + "/" + kMiscInfoPath;
  const auto combined_info =
      CF_EXPECT(CombineMiscInfo(target_files, misc_output_path,
                                extracted.images, extracted.system_partitions));
  CF_EXPECT(RegenerateVbmeta(combined_info, paths.vbmeta_image_output,
                             paths.combined_target_zip));
  return {};
}

bool BuildSuperImage(const std::string& combined_target_zip,
                     const std::string& output_path) {
  std::string otatools_path = DefaultHostArtifactsPath("");
  std::string build_super_image_binary = HostBinaryPath("build_super_image");
  if (!FileExists(build_super_image_binary)) {
    LOG(ERROR) << "Could not find build_super_image";
    return false;
  }
  return Execute({
             build_super_image_binary,
             "--path=" + otatools_path,
             combined_target_zip,
             output_path,
         }) == 0;
}

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

Result<RebuildPaths> GetRebuildPaths(
    const FetcherConfig& fetcher_config,
    const CuttlefishConfig::InstanceSpecific& instance_config) {
  // In SuperImageNeedsRebuilding, it already checked that both paths either
  // exist or do not exist, together Here, we only check if there is an input
  // path
  std::string default_target_zip = instance_config.default_target_zip();
  std::string system_target_zip = instance_config.system_target_zip();
  if (default_target_zip == "" || default_target_zip == "unset") {
    default_target_zip =
        TargetFilesZip(fetcher_config, FileSource::DEFAULT_BUILD);
    CF_EXPECT(default_target_zip != "",
              "Unable to find default target zip file.");

    system_target_zip =
        TargetFilesZip(fetcher_config, FileSource::SYSTEM_BUILD);
    CF_EXPECT(system_target_zip != "", "Unable to find system target zip file.");
  }
  return RebuildPaths{
      .vendor_target_zip = default_target_zip,
      .system_target_zip = system_target_zip,
      // TODO(schuffelen): Use cuttlefish_assembly
      .combined_target_zip =
          instance_config.PerInstanceInternalPath("target_combined"),
      .super_image_output = instance_config.new_super_image(),
      .vbmeta_image_output = instance_config.new_vbmeta_image(),
  };
}

Result<void> RebuildSuperImage(const RebuildPaths& paths) {
  // TODO(schuffelen): Use otatools/bin/merge_target_files
  CF_EXPECT(CombineTargetZipFiles(paths),
            "Could not combine target zip files.");

  CF_EXPECT(
      BuildSuperImage(paths.combined_target_zip, paths.super_image_output),
      "Could not write the final output super image.");
  return {};
}

class SuperImageRebuilderImpl : public SuperImageRebuilder {
 public:
  INJECT(SuperImageRebuilderImpl(
      const FetcherConfig& fetcher_config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : fetcher_config_(fetcher_config), instance_(instance) {}

  std::string Name() const override { return "SuperImageRebuilderImpl"; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (CF_EXPECT(SuperImageNeedsRebuilding(fetcher_config_,
                                            instance_.default_target_zip(),
                                            instance_.system_target_zip()))) {
      const RebuildPaths paths =
          CF_EXPECT(GetRebuildPaths(fetcher_config_, instance_));
      LOG(INFO) << "The super.img is being rebuilt with provided vendor and "
                   "system target files.";
      LOG(INFO) << "Vendor target files at: " << paths.vendor_target_zip;
      LOG(INFO) << "System target files at: " << paths.system_target_zip;
      CF_EXPECT(RebuildSuperImage(paths));
      LOG(INFO) << "Rebuild complete.";
      LOG(INFO) << "Combined target files at: " << paths.combined_target_zip;
      LOG(INFO) << "New super.img at: " << paths.super_image_output;
      LOG(INFO) << "New vbmeta.img at: " << paths.vbmeta_image_output;
    }
    return {};
  }

  const FetcherConfig& fetcher_config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

}  // namespace

Result<bool> SuperImageNeedsRebuilding(const FetcherConfig& fetcher_config,
                                       const std::string& default_target_zip,
                                       const std::string& system_target_zip) {
  bool has_default_target_zip = false;
  bool has_system_target_zip = false;
  if (default_target_zip != "" && default_target_zip != "unset") {
    has_default_target_zip = true;
  }
  if (system_target_zip != "" && system_target_zip != "unset") {
    has_system_target_zip = true;
  }
  CF_EXPECT(has_default_target_zip == has_system_target_zip,
            "default_target_zip and system_target_zip "
            "flags must be specified together");
  // at this time, both should be the same, either true or false
  // therefore, I only check one variable
  if (has_default_target_zip) {
    return true;
  }

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

fruit::Component<fruit::Required<const FetcherConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 SuperImageRebuilder>
SuperImageRebuilderComponent() {
  return fruit::createComponent()
      .bind<SuperImageRebuilder, SuperImageRebuilderImpl>()
      .addMultibinding<SetupFeature, SuperImageRebuilder>();
}

}  // namespace cuttlefish
