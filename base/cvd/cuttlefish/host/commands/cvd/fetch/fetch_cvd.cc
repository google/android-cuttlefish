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

#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd.h"

#include <android-base/file.h>
#include <sys/stat.h>

#include <cstddef>
#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/archive.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/fetch/build_strings.h"
#include "cuttlefish/host/commands/cvd/fetch/de_android_sparse.h"
#include "cuttlefish/host/commands/cvd/fetch/download_flags.h"
#include "cuttlefish/host/commands/cvd/fetch/downloaders.h"
#include "cuttlefish/host/commands/cvd/fetch/extract_image_contents.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_tracer.h"
#include "cuttlefish/host/commands/cvd/fetch/host_package.h"
#include "cuttlefish/host/commands/cvd/fetch/host_tools_target.h"
#include "cuttlefish/host/commands/cvd/fetch/target_directories.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image_utils.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_zip_name.h"
#include "cuttlefish/host/libs/web/chrome_os_build_string.h"
#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"
#include "cuttlefish/host/libs/web/luci_build_api.h"

namespace cuttlefish {
namespace {

constexpr mode_t kRwxAllMode = S_IRWXU | S_IRWXG | S_IRWXO;
constexpr bool kOverrideEntries = true;

struct Builds {
  std::optional<Build> default_build;
  std::optional<Build> system;
  std::optional<Build> kernel;
  std::optional<Build> boot;
  std::optional<Build> bootloader;
  std::optional<Build> android_efi_loader;
  std::optional<Build> otatools;
  std::optional<ChromeOsBuildString> chrome_os;
};

struct Target {
  BuildStrings build_strings;
  DownloadFlags download_flags;
  TargetDirectories directories;
  Builds builds;
};

bool ShouldAppendSubdirectory(const FetchFlags& flags) {
  return flags.vector_flags.NumberOfBuilds().value_or(1) > 1 ||
         !flags.vector_flags.target_subdirectory.empty();
}

std::vector<Target> GetFetchTargets(const FetchFlags& flags,
                                    const bool append_subdirectory) {
  std::vector<Target> result(flags.vector_flags.NumberOfBuilds().value_or(1));
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i] = Target{
        .build_strings = BuildStrings::Create(flags.vector_flags, i),
        .download_flags = DownloadFlags::Create(flags.vector_flags, i),
        .directories = TargetDirectories::Create(
            flags.target_directory, flags.vector_flags.target_subdirectory, i,
            append_subdirectory),
    };
  }
  return result;
}

Result<void> EnsureDirectoriesExist(const std::string& host_tools_directory,
                                    const std::vector<Target>& targets) {
  CF_EXPECT(EnsureDirectoryExists(host_tools_directory));
  for (const auto& target : targets) {
    CF_EXPECT(EnsureDirectoryExists(target.directories.root, kRwxAllMode));
    CF_EXPECT(EnsureDirectoryExists(target.directories.otatools, kRwxAllMode));
    CF_EXPECT(EnsureDirectoryExists(target.directories.default_target_files,
                                    kRwxAllMode));
    CF_EXPECT(EnsureDirectoryExists(target.directories.system_target_files,
                                    kRwxAllMode));
    CF_EXPECT(EnsureDirectoryExists(target.directories.chrome_os, kRwxAllMode));
  }
  return {};
}

Result<std::vector<std::string>> FetchSystemImgZipImages(
    BuildApi& build_api, const Build& build,
    const std::string& target_directory, const bool keep_downloaded_archives) {
  LOG(INFO) << "Downloading system image zip for " << build;
  const std::string system_img_zip_name = GetBuildZipName(build, "img");
  std::string system_img_zip = CF_EXPECTF(
      build_api.DownloadFile(build, target_directory, system_img_zip_name),
      "Unable to download {}", system_img_zip_name);
  return CF_EXPECTF(
      ExtractImages(system_img_zip, target_directory,
                    {"system.img", "product.img"}, keep_downloaded_archives),
      "Unable to extract system and product images from {}",
      system_img_zip_name);
}

Result<std::optional<Build>> GetBuildHelper(
    BuildApi& build_api, const std::optional<BuildString>& build_source,
    const std::string& fallback_target) {
  if (!build_source) {
    return std::nullopt;
  }
  BuildString source = WithFallbackTarget(*build_source, fallback_target);
  return CF_EXPECT(build_api.GetBuild(source),
                   "Unable to create build from (" << source << ")");
}

Result<Builds> GetBuilds(BuildApi& build_api,
                         const BuildStrings& build_sources) {
  Builds result = Builds{
      .default_build = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.default_build, kDefaultBuildTarget)),
      .system = CF_EXPECT(GetBuildHelper(build_api, build_sources.system_build,
                                         kDefaultBuildTarget)),
      .kernel = CF_EXPECT(
          GetBuildHelper(build_api, build_sources.kernel_build, "kernel")),
      .boot = CF_EXPECT(GetBuildHelper(build_api, build_sources.boot_build,
                                       "gki_x86_64-user")),
      .bootloader = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.bootloader_build, "u-boot_crosvm_x86_64")),
      .android_efi_loader = CF_EXPECT(
          GetBuildHelper(build_api, build_sources.android_efi_loader_build,
                         "gbl_efi_dist_and_test")),
      .otatools = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.otatools_build, kDefaultBuildTarget)),
      .chrome_os = build_sources.chrome_os_build,
  };
  if (!result.otatools) {
    if (result.system) {
      result.otatools = result.system;
    } else if (result.kernel) {
      result.otatools = result.default_build;
    }
  }
  return {result};
}

Result<void> UpdateTargetsWithBuilds(BuildApi& build_api,
                                     std::vector<Target>& targets) {
  for (auto& target : targets) {
    target.builds = CF_EXPECT(GetBuilds(build_api, target.build_strings));
  }
  return {};
}

Result<Build> GetHostBuild(BuildApi& build_api,
                           const HostToolsTarget& host_target,
                           const std::optional<Build>& fallback_host_build) {
  auto host_package_build = CF_EXPECT(
      GetBuildHelper(build_api, host_target.build_string, kDefaultBuildTarget));
  CF_EXPECT(host_package_build.has_value() || fallback_host_build.has_value(),
            "Either `--host_package_build` or `--default_build` needs to be "
            "specified. Try "
            "`--default_build=aosp-android-latest-release/"
            "aosp_cf_x86_64_only_phone-userdebug");
  return host_package_build.value_or(*fallback_host_build);
}

Result<void> SaveConfig(FetcherConfig& config,
                        const std::string& target_directory) {
  // Due to constraints of the build system, artifacts intentionally cannot
  // determine their own build id. So it's unclear which build number fetch_cvd
  // itself was built at.
  // https://android.googlesource.com/platform/build/+/979c9f3/Changes.md#build_number
  std::string fetcher_path = target_directory + "/fetcher_config.json";
  CF_EXPECT(config.AddFilesToConfig(FileSource::GENERATED, "", "",
                                    {fetcher_path}, target_directory));
  config.SaveToFile(fetcher_path);

  for (const auto& file : config.get_cvd_files()) {
    LOG(VERBOSE) << target_directory << "/" << file.second.file_path << "\n";
  }
  return {};
}

Result<void> FetchDefaultTarget(BuildApi& build_api, const Builds& builds,
                                const TargetDirectories& target_directories,
                                const DownloadFlags& flags,
                                const bool keep_downloaded_archives,
                                FetcherConfig& config,
                                FetchTracer::Trace trace) {
  const auto [default_build_id, default_build_target] =
      GetBuildIdAndTarget(*builds.default_build);

  // Some older builds might not have misc_info.txt, so permit errors on
  // fetching misc_info.txt
  Result<std::string> misc_info_result = build_api.DownloadFile(
      *builds.default_build, target_directories.root, "misc_info.txt");
  trace.CompletePhase("Download misc_info.txt");
  if (misc_info_result.ok()) {
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::DEFAULT_BUILD, default_build_id, default_build_target,
        {misc_info_result.value()}, target_directories.root, kOverrideEntries));
  }

  if (flags.download_img_zip) {
    LOG(INFO) << "Downloading image zip for " << *builds.default_build;
    std::string img_zip_name = GetBuildZipName(*builds.default_build, "img");
    std::string default_img_zip_filepath = CF_EXPECT(build_api.DownloadFile(
        *builds.default_build, target_directories.root, img_zip_name));
    trace.CompletePhase("Download image zip",
                        FileSize(default_img_zip_filepath));
    std::vector<std::string> image_files = CF_EXPECT(
        ExtractImageContents(default_img_zip_filepath, target_directories.root,
                             keep_downloaded_archives));
    trace.CompletePhase("Extract image zip contents");
    LOG(DEBUG) << "Adding img-zip files for default build";
    for (auto& file : image_files) {
      LOG(VERBOSE) << file;
    }
    CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                      default_build_id, default_build_target,
                                      image_files, target_directories.root));
    CF_EXPECT(DeAndroidSparse2(image_files));
    trace.CompletePhase("Desparse image files");
  }

  if (builds.system || flags.download_target_files_zip) {
    LOG(INFO) << "Downloading target files zip for " << *builds.default_build;
    std::string target_files_name =
        GetBuildZipName(*builds.default_build, "target_files");
    std::string target_files = CF_EXPECT(build_api.DownloadFile(
        *builds.default_build, target_directories.default_target_files,
        target_files_name));
    trace.CompletePhase("Download Target Files");
    LOG(INFO) << "Adding target files for default build";
    CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                      default_build_id, default_build_target,
                                      {target_files}, target_directories.root));
  }
  return {};
}

Result<void> FetchSystemTarget(BuildApi& build_api, const Build& system_build,
                               const TargetDirectories& target_directories,
                               bool download_img_zip,
                               const bool keep_downloaded_archives,
                               FetcherConfig& config,
                               FetchTracer::Trace trace) {
  std::string target_files_name =
      GetBuildZipName(system_build, "target_files");
  std::string target_files = CF_EXPECT(build_api.DownloadFile(
      system_build, target_directories.system_target_files, target_files_name));
  trace.CompletePhase("Download Target Files", FileSize(target_files_name));
  const auto [system_id, system_target] = GetBuildIdAndTarget(system_build);
  CF_EXPECT(config.AddFilesToConfig(FileSource::SYSTEM_BUILD, system_id,
                                    system_target, {target_files},
                                    target_directories.root));

  if (download_img_zip) {
    LOG(INFO) << "Downloading system image zip for " << system_build;
    std::vector<std::string> system_images;
    Result<std::string> extracted_system = ExtractImage(
        target_files, target_directories.root, "IMAGES/system.img");
    if (extracted_system.ok()) {
      const std::string system_path = target_directories.root + "/system.img";
      CF_EXPECT(RenameFile(*extracted_system, system_path));
      system_images.emplace_back(system_path);
      trace.CompletePhase("Extract system image");
    } else {
      LOG(INFO) << "Unable to retrieve system.img from target files, falling "
                   "back to system *-img-*.zip for system image";
      const auto img_zip_images =
          CF_EXPECT(FetchSystemImgZipImages(build_api, system_build,
                                            target_directories.root,
                                            keep_downloaded_archives),
                    "Unable to retrieve system images from fallback to "
                    "system *-img-*.zip");
      for (const auto& image_path : img_zip_images) {
        system_images.emplace_back(image_path);
      }
      size_t size = 0;
      for (const auto& img : img_zip_images) {
        size += FileSize(img);
      }
      trace.CompletePhase("Fetch system images fallback", size);
    }

    Result<std::string> extracted_product = ExtractImage(
        target_files, target_directories.root, "IMAGES/product.img");
    if (extracted_product.ok()) {
      const std::string product_path = target_directories.root + "/product.img";
      CF_EXPECT(RenameFile(*extracted_product, product_path));
      system_images.emplace_back(product_path);
      trace.CompletePhase("Extract product image");
    }

    Result<std::string> extracted_system_ext = ExtractImage(
        target_files, target_directories.root, "IMAGES/system_ext.img");
    if (extracted_system_ext.ok()) {
      const std::string system_ext_path =
          target_directories.root + "/system_ext.img";
      CF_EXPECT(RenameFile(*extracted_system_ext, system_ext_path));
      system_images.emplace_back(system_ext_path);
      trace.CompletePhase("Extract system_ext image");
    }

    Result<std::string> extracted_vbmeta_system = ExtractImage(
        target_files, target_directories.root, "IMAGES/vbmeta_system.img");
    if (extracted_vbmeta_system.ok()) {
      const std::string vbmeta_system_path =
          target_directories.root + "/vbmeta_system.img";
      CF_EXPECT(RenameFile(*extracted_vbmeta_system, vbmeta_system_path));
      system_images.emplace_back(vbmeta_system_path);
      trace.CompletePhase("Extract vbmeta_system image");
    }

    Result<std::string> extracted_init_boot = ExtractImage(
        target_files, target_directories.root, "IMAGES/init_boot.img");
    if (extracted_init_boot.ok()) {
      const std::string init_boot_path =
          target_directories.root + "/init_boot.img";
      CF_EXPECT(RenameFile(*extracted_init_boot, init_boot_path));
      system_images.emplace_back(init_boot_path);
      trace.CompletePhase("Extract init_boot image");
    }

    CF_EXPECT(config.AddFilesToConfig(
        FileSource::SYSTEM_BUILD, system_id, system_target, system_images,
        target_directories.root, kOverrideEntries));
    DeAndroidSparse2(system_images);
  }
  return {};
}

Result<void> FetchKernelTarget(BuildApi& build_api, const Build& kernel_build,
                               const std::string& target_directory,
                               FetcherConfig& config,
                               FetchTracer::Trace trace) {
  std::string kernel_filepath = target_directory + "/kernel";
  // If the kernel is from an arm/aarch64 build, the artifact will be called
  // Image.
  std::string downloaded_kernel_filepath =
      CF_EXPECT(build_api.DownloadFileWithBackup(kernel_build, target_directory,
                                                 "bzImage", "Image"));
  trace.CompletePhase("Download bzImage", FileSize(downloaded_kernel_filepath));
  CF_EXPECT(RenameFile(downloaded_kernel_filepath, kernel_filepath));
  const auto [kernel_id, kernel_target] = GetBuildIdAndTarget(kernel_build);
  CF_EXPECT(config.AddFilesToConfig(FileSource::KERNEL_BUILD, kernel_id,
                                    kernel_target, {kernel_filepath},
                                    target_directory));
  DeAndroidSparse2({kernel_filepath});
  trace.CompletePhase("Desparse bzImage");

  // Certain kernel builds do not have corresponding ramdisks.
  Result<std::string> initramfs_img_result =
      build_api.DownloadFile(kernel_build, target_directory, "initramfs.img");
  if (initramfs_img_result.ok()) {
    trace.CompletePhase("Download initramfs",
                        FileSize(initramfs_img_result.value()));
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::KERNEL_BUILD, kernel_id, kernel_target,
        {initramfs_img_result.value()}, target_directory));
    DeAndroidSparse2({initramfs_img_result.value()});
  }
  return {};
}

Result<void> FetchBootTarget(BuildApi& build_api, const Build& boot_build,
                             const std::string& target_directory,
                             const bool keep_downloaded_archives,
                             FetcherConfig& config, FetchTracer::Trace trace) {
  std::string boot_img_zip_name = GetBuildZipName(boot_build, "img");
  std::string downloaded_boot_filepath;
  std::optional<std::string> boot_filepath = GetFilepath(boot_build);
  if (boot_filepath) {
    downloaded_boot_filepath = CF_EXPECT(build_api.DownloadFileWithBackup(
        boot_build, target_directory, *boot_filepath, boot_img_zip_name));
  } else {
    downloaded_boot_filepath = CF_EXPECT(build_api.DownloadFile(
        boot_build, target_directory, boot_img_zip_name));
  }
  trace.CompletePhase("Download", FileSize(downloaded_boot_filepath));

  std::vector<std::string> boot_files;
  // downloaded a zip that needs to be extracted
  if (android::base::EndsWith(downloaded_boot_filepath, boot_img_zip_name)) {
    std::string extract_target = boot_filepath.value_or("boot.img");
    std::string extracted_boot = CF_EXPECT(ExtractImage(
        downloaded_boot_filepath, target_directory, extract_target));
    std::string target_boot =
        CF_EXPECT(RenameFile(extracted_boot, target_directory + "/boot.img"));
    boot_files.push_back(target_boot);
    trace.CompletePhase("Extract boot image");

    // keep_downloaded_archives flag used because this is the last extract
    // on this archive
    Result<std::string> extracted_vendor_boot_result =
        ExtractImage(downloaded_boot_filepath, target_directory,
                     "vendor_boot.img", keep_downloaded_archives);
    if (extracted_vendor_boot_result.ok()) {
      trace.CompletePhase("Extract vendor boot image");
      boot_files.push_back(extracted_vendor_boot_result.value());
    }
  } else {
    boot_files.push_back(downloaded_boot_filepath);
  }
  const auto [boot_id, boot_target] = GetBuildIdAndTarget(boot_build);
  CF_EXPECT(config.AddFilesToConfig(FileSource::BOOT_BUILD, boot_id,
                                    boot_target, boot_files, target_directory,
                                    kOverrideEntries));
  DeAndroidSparse2(boot_files);
  trace.CompletePhase("Desparse");
  return {};
}

Result<void> FetchBootloaderTarget(BuildApi& build_api,
                                   const Build& bootloader_build,
                                   const std::string& target_directory,
                                   FetcherConfig& config,
                                   FetchTracer::Trace trace) {
  std::string bootloader_filepath = target_directory + "/bootloader";
  // If the bootloader is from an arm/aarch64 build, the artifact will be of
  // filetype bin.
  std::string downloaded_bootloader_filepath =
      CF_EXPECT(build_api.DownloadFileWithBackup(
          bootloader_build, target_directory, "u-boot.rom", "u-boot.bin"));
  trace.CompletePhase("Download", FileSize(downloaded_bootloader_filepath));
  CF_EXPECT(RenameFile(downloaded_bootloader_filepath, bootloader_filepath));
  const auto [bootloader_id, bootloader_target] =
      GetBuildIdAndTarget(bootloader_build);
  CF_EXPECT(config.AddFilesToConfig(FileSource::BOOTLOADER_BUILD, bootloader_id,
                                    bootloader_target, {bootloader_filepath},
                                    target_directory, kOverrideEntries));
  DeAndroidSparse2({bootloader_filepath});
  trace.CompletePhase("Desparse image");
  return {};
}

Result<void> FetchAndroidEfiLoaderTarget(BuildApi& build_api,
                                         const Build& android_efi_loader_build,
                                         const std::string& target_directory,
                                         FetcherConfig& config,
                                         FetchTracer::Trace trace) {
  std::string android_efi_loader_target_filepath =
      target_directory + "/android_efi_loader.efi";
  std::optional<std::string> android_efi_loader_filepath =
      GetFilepath(android_efi_loader_build);

  std::string downloaded_android_efi_loader_filepath =
      CF_EXPECT(build_api.DownloadFile(
          android_efi_loader_build, target_directory,
          android_efi_loader_filepath.value_or("gbl_x86_64.efi")));
  trace.CompletePhase("Download",
                      FileSize(downloaded_android_efi_loader_filepath));
  CF_EXPECT(RenameFile(downloaded_android_efi_loader_filepath,
                       android_efi_loader_target_filepath));

  const auto [android_efi_loader_id, android_efi_loader_target] =
      GetBuildIdAndTarget(android_efi_loader_build);
  CF_EXPECT(config.AddFilesToConfig(
      FileSource::ANDROID_EFI_LOADER_BUILD, android_efi_loader_id,
      android_efi_loader_target, {android_efi_loader_target_filepath},
      target_directory, kOverrideEntries));
  DeAndroidSparse2({android_efi_loader_target_filepath});
  trace.CompletePhase("Desparse image");
  return {};
}

Result<void> FetchOtaToolsTarget(BuildApi& build_api,
                                 const Build& otatools_build,
                                 const TargetDirectories& target_directories,
                                 const bool keep_downloaded_archives,
                                 FetcherConfig& config,
                                 FetchTracer::Trace trace) {
  std::string otatools_filepath = CF_EXPECT(build_api.DownloadFile(
      otatools_build, target_directories.root, "otatools.zip"));
  trace.CompletePhase("Download", FileSize(otatools_filepath));
  std::vector<std::string> ota_tools_files = CF_EXPECT(
      ExtractArchiveContents(otatools_filepath, target_directories.otatools,
                             keep_downloaded_archives));
  trace.CompletePhase("Extract");
  const auto [otatools_build_id, otatools_build_target] =
      GetBuildIdAndTarget(otatools_build);
  CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                    otatools_build_id, otatools_build_target,
                                    ota_tools_files, target_directories.root));
  DeAndroidSparse2(ota_tools_files);
  trace.CompletePhase("Desparse files");
  return {};
}

Result<void> FetchChromeOsTarget(
    LuciBuildApi& luci_build_api,
    const ChromeOsBuildString& chrome_os_build_string,
    const TargetDirectories& target_directories,
    const bool keep_downloaded_archives, FetcherConfig& config,
    FetchTracer::Trace trace) {
  auto artifacts_opt =
      CF_EXPECT(luci_build_api.GetBuildArtifacts(chrome_os_build_string));
  auto artifacts = CF_EXPECT(std::move(artifacts_opt));
  trace.CompletePhase("Get build artifacts");
  std::string archive_name = "chromiumos_test_image.tar.xz";
  CF_EXPECT(Contains(artifacts.artifact_files, archive_name));
  auto archive_path = target_directories.root + "/" + archive_name;
  CF_EXPECT(luci_build_api.DownloadArtifact(artifacts.artifact_link,
                                            archive_name, archive_path));
  trace.CompletePhase("Download test image", FileSize(archive_path));
  auto archive_files = CF_EXPECT(ExtractArchiveContents(
      archive_path, target_directories.chrome_os, keep_downloaded_archives));
  trace.CompletePhase("Extract");
  CF_EXPECT(config.AddFilesToConfig(FileSource::CHROME_OS_BUILD, "", "",
                                    archive_files, target_directories.root));
  return {};
}

Result<void> FetchTarget(BuildApi& build_api, LuciBuildApi& luci_build_api,
                         const Builds& builds,
                         const TargetDirectories& target_directories,
                         const DownloadFlags& flags,
                         const bool keep_downloaded_archives,
                         FetcherConfig& config, FetchTracer& tracer) {
  if (builds.default_build) {
    CF_EXPECT(FetchDefaultTarget(build_api, builds, target_directories, flags,
                                 keep_downloaded_archives, config,
                                 tracer.NewTrace("Default")));
  }

  if (builds.system) {
    CF_EXPECT(FetchSystemTarget(
        build_api, *builds.system, target_directories, flags.download_img_zip,
        keep_downloaded_archives, config, tracer.NewTrace("System")));
  }

  if (builds.kernel) {
    CF_EXPECT(FetchKernelTarget(build_api, *builds.kernel,
                                target_directories.root, config,
                                tracer.NewTrace("Kernel")));
  }

  if (builds.boot) {
    CF_EXPECT(FetchBootTarget(build_api, *builds.boot, target_directories.root,
                              keep_downloaded_archives, config,
                              tracer.NewTrace("Boot")));
  }

  if (builds.bootloader) {
    CF_EXPECT(FetchBootloaderTarget(build_api, *builds.bootloader,
                                    target_directories.root, config,
                                    tracer.NewTrace("Bootloader")));
  }

  if (builds.android_efi_loader) {
    CF_EXPECT(FetchAndroidEfiLoaderTarget(
        build_api, *builds.android_efi_loader, target_directories.root, config,
        tracer.NewTrace("Android EFI Loader")));
  }

  if (builds.otatools) {
    CF_EXPECT(FetchOtaToolsTarget(build_api, *builds.otatools,
                                  target_directories, keep_downloaded_archives,
                                  config, tracer.NewTrace("OTA Tools")));
  }

  if (builds.chrome_os) {
    CF_EXPECT(FetchChromeOsTarget(luci_build_api, *builds.chrome_os,
                                  target_directories, keep_downloaded_archives,
                                  config, tracer.NewTrace("ChromeOS")));
  }

  return {};
}

Result<void> Fetch(const FetchFlags& flags, const HostToolsTarget& host_target,
                   std::vector<Target>& targets) {
#ifdef __BIONIC__
  // TODO(schuffelen): Find a better way to deal with tzdata
  setenv("ANDROID_TZDATA_ROOT", "/", /* overwrite */ 0);
  setenv("ANDROID_ROOT", "/", /* overwrite */ 0);
#endif
  CurlGlobalInit curl_init;

  Downloaders downloaders =
      CF_EXPECT(Downloaders::Create(flags.build_api_flags));

  FetchTracer tracer;
  FetchTracer::Trace prefetch_trace = tracer.NewTrace("PreFetch actions");
  CF_EXPECT(UpdateTargetsWithBuilds(downloaders.AndroidBuild(), targets));
  std::optional<Build> fallback_host_build = std::nullopt;
  if (!targets.empty()) {
    fallback_host_build = targets[0].builds.default_build;
  }
  const auto host_target_build = CF_EXPECT(GetHostBuild(
      downloaders.AndroidBuild(), host_target, fallback_host_build));
  prefetch_trace.CompletePhase("GetBuilds");

  auto host_package_future = std::async(
      std::launch::async, FetchHostPackage,
      std::ref(downloaders.AndroidBuild()), std::cref(host_target_build),
      std::cref(host_target.host_tools_directory),
      std::cref(flags.keep_downloaded_archives),
      std::cref(flags.host_substitutions), tracer.NewTrace("Host Package"));
  size_t count = 1;
  for (const auto& target : targets) {
    LOG(INFO) << "Starting fetch to \"" << target.directories.root << "\"";
    FetcherConfig config;
    CF_EXPECT(FetchTarget(downloaders.AndroidBuild(), downloaders.Luci(),
                          target.builds, target.directories,
                          target.download_flags, flags.keep_downloaded_archives,
                          config, tracer));
    CF_EXPECT(SaveConfig(config, target.directories.root));
    LOG(INFO) << "Completed target fetch to '" << target.directories.root
              << "' (" << count << " out of " << targets.size() << ")";
    count++;
  }
  LOG(DEBUG) << "Waiting for host package fetch";
  CF_EXPECT(host_package_future.get());
  LOG(DEBUG) << "Performance stats:\n" << tracer.ToStyledString();

  LOG(INFO) << "Completed all fetches";
  return {};
}

}  // namespace

std::string GetFetchLogsFileName(const std::string& target_directory) {
  return target_directory + "/fetch.log";
}

Result<void> FetchCvdMain(const FetchFlags& flags) {
  const bool append_subdirectory = ShouldAppendSubdirectory(flags);
  std::vector<Target> targets = GetFetchTargets(flags, append_subdirectory);
  HostToolsTarget host_target =
      HostToolsTarget::Create(flags, append_subdirectory);
  CF_EXPECT(EnsureDirectoriesExist(host_target.host_tools_directory, targets));
  CF_EXPECT(Fetch(flags, host_target, targets));
  return {};
}

}  // namespace cuttlefish
