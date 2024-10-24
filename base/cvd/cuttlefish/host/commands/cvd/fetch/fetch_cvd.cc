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

#include "host/commands/cvd/fetch/fetch_cvd.h"

#include <sys/stat.h>

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <curl/curl.h>
#include <sparse/sparse.h>

#include "common/libs/utils/archive.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "host/commands/cvd/fetch/fetch_tracer.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/image_aggregator/sparse_image_utils.h"
#include "host/libs/web/android_build_api.h"
#include "host/libs/web/android_build_string.h"
#include "host/libs/web/caching_build_api.h"
#include "host/libs/web/chrome_os_build_string.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"
#include "host/libs/web/luci_build_api.h"

namespace cuttlefish {
namespace {

constexpr mode_t kRwxAllMode = S_IRWXU | S_IRWXG | S_IRWXO;
constexpr bool kOverrideEntries = true;

struct BuildStrings {
  std::optional<BuildString> default_build;
  std::optional<BuildString> system_build;
  std::optional<BuildString> kernel_build;
  std::optional<BuildString> boot_build;
  std::optional<BuildString> bootloader_build;
  std::optional<BuildString> android_efi_loader_build;
  std::optional<BuildString> otatools_build;
  std::optional<BuildString> host_package_build;
  std::optional<ChromeOsBuildString> chrome_os_build;
};

struct DownloadFlags {
  bool download_img_zip;
  bool download_target_files_zip;
};

struct TargetDirectories {
  std::string root;
  std::string otatools;
  std::string default_target_files;
  std::string system_target_files;
  std::string chrome_os;
};

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

struct HostToolsTarget {
  std::optional<BuildString> build_string;
  std::string host_tools_directory;
};

bool ShouldAppendSubdirectory(const FetchFlags& flags) {
  return flags.number_of_builds > 1 || !flags.target_subdirectory.empty();
}

template <typename T>
T AccessOrDefault(const std::vector<T>& vector, const std::size_t i,
                  const T& default_value) {
  if (i < vector.size()) {
    return vector[i];
  } else {
    return default_value;
  }
}

BuildStrings GetBuildStrings(const VectorFlags& flags, const int index) {
  auto build_strings = BuildStrings{
      .default_build = AccessOrDefault<std::optional<BuildString>>(
          flags.default_build, index, std::nullopt),
      .system_build = AccessOrDefault<std::optional<BuildString>>(
          flags.system_build, index, std::nullopt),
      .kernel_build = AccessOrDefault<std::optional<BuildString>>(
          flags.kernel_build, index, std::nullopt),
      .boot_build = AccessOrDefault<std::optional<BuildString>>(
          flags.boot_build, index, std::nullopt),
      .bootloader_build = AccessOrDefault<std::optional<BuildString>>(
          flags.bootloader_build, index, std::nullopt),
      .android_efi_loader_build = AccessOrDefault<std::optional<BuildString>>(
          flags.android_efi_loader_build, index, std::nullopt),
      .otatools_build = AccessOrDefault<std::optional<BuildString>>(
          flags.otatools_build, index, std::nullopt),
      .chrome_os_build = AccessOrDefault<std::optional<ChromeOsBuildString>>(
          flags.chrome_os_build, index, std::nullopt),
  };
  auto possible_boot_artifact =
      AccessOrDefault<std::string>(flags.boot_artifact, index, "");
  if (!possible_boot_artifact.empty() && build_strings.boot_build) {
    SetFilepath(*build_strings.boot_build, possible_boot_artifact);
  }
  return build_strings;
}

DownloadFlags GetDownloadFlags(const VectorFlags& flags, const int index) {
  return DownloadFlags{
      .download_img_zip = AccessOrDefault<bool>(flags.download_img_zip, index,
                                                kDefaultDownloadImgZip),
      .download_target_files_zip =
          AccessOrDefault<bool>(flags.download_target_files_zip, index,
                                kDefaultDownloadTargetFilesZip),
  };
}

TargetDirectories GetTargetDirectories(
    const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories, const int index,
    const bool append_subdirectory) {
  std::string base_directory = target_directory;
  if (append_subdirectory) {
    base_directory +=
        "/" + AccessOrDefault<std::string>(target_subdirectories, index,
                                           "instance_" + std::to_string(index));
  }
  return TargetDirectories{.root = base_directory,
                           .otatools = base_directory + "/otatools/",
                           .default_target_files = base_directory + "/default",
                           .system_target_files = base_directory + "/system",
                           .chrome_os = base_directory + "/chromeos"};
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

bool ConvertToRawImageNoBinary(const std::string& image_path) {
  std::string tmp_raw_image_path = image_path + ".raw";

  // simg2img logic to convert sparse image to raw image.
  struct sparse_file* s;
  int out = open(tmp_raw_image_path.c_str(),
                 O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0664);
  int in = open(image_path.c_str(), O_RDONLY | O_BINARY);
  if (in < 0) {
    LOG(FATAL) << "Cannot open input file " << image_path;
    return false;
  }

  s = sparse_file_import(in, true, false);
  if (!s) {
    LOG(FATAL) << "Failed to read sparse file " << image_path;
    return false;
  }

  if (lseek(out, 0, SEEK_SET) == -1) {
    LOG(FATAL) << "lseek failed " << tmp_raw_image_path;
    return false;
  }

  if (sparse_file_write(s, out, false, false, false) < 0) {
    LOG(FATAL) << "Cannot write output file " << image_path;
    return false;
  }
  sparse_file_destroy(s);
  close(in);
  close(out);

  // Replace the original sparse image with the raw image.
  if (unlink(image_path.c_str()) != 0) {
    PLOG(FATAL) << "Unable to delete original sparse image";
  }

  int success = rename(tmp_raw_image_path.c_str(), image_path.c_str());
  if (success != 0) {
    LOG(FATAL) << "Unable to rename raw image " << success;
    return false;
  }

  return true;
}

/**
 * Converts any Android-Sparse image files in `image_files` to raw image files.
 *
 * Android-Sparse is a file format invented by Android that optimizes for
 * chunks of zeroes or repeated data. The Android build system can produce
 * sparse files to save on size of disk files after they are extracted from a
 * disk file, as the imag eflashing process also can handle Android-Sparse
 * images.
 *
 * crosvm has read-only support for Android-Sparse files, but QEMU does not
 * support them.
 */
void DeAndroidSparse(const std::vector<std::string>& image_files) {
  for (const auto& file : image_files) {
    if (!IsSparseImage(file)) {
      continue;
    }
    if (ConvertToRawImageNoBinary(file)) {
      LOG(DEBUG) << "De-sparsed '" << file << "'";
    } else {
      LOG(ERROR) << "Failed to de-sparse '" << file << "'";
    }
  }
}

std::vector<Target> GetFetchTargets(const FetchFlags& flags,
                                    const bool append_subdirectory) {
  std::vector<Target> result(flags.number_of_builds);
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i] = Target{
        .build_strings = GetBuildStrings(flags.vector_flags, i),
        .download_flags = GetDownloadFlags(flags.vector_flags, i),
        .directories = GetTargetDirectories(flags.target_directory,
                                            flags.target_subdirectory, i,
                                            append_subdirectory),
    };
  }
  return result;
}

HostToolsTarget GetHostToolsTarget(const FetchFlags& flags,
                                   const bool append_subdirectory) {
  std::string host_directory = flags.target_directory;
  if (append_subdirectory) {
    host_directory = host_directory + "/" + kHostToolsSubdirectory;
  }
  return HostToolsTarget{
      .build_string = flags.host_package_build,
      .host_tools_directory = host_directory,
  };
}

Result<void> EnsureDirectoriesExist(const std::string& target_directory,
                                    const std::string& host_tools_directory,
                                    const std::vector<Target>& targets) {
  CF_EXPECT(EnsureDirectoryExists(target_directory));
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

Result<void> FetchHostPackage(IBuildApi& build_api, const Build& build,
                              const std::string& target_dir,
                              const bool keep_archives,
                              FetchTracer::Trace trace) {
  LOG(INFO) << "Preparing host package for " << build;
  // This function is called asynchronously, so it may take a while to start.
  // Complete a phase here to ensure that delay is not counted in the download
  // time.
  // The download time will still include time spent waiting for the mutex in
  // the build_api though.
  trace.CompletePhase("Async start delay");
  auto host_tools_name = GetFilepath(build).value_or("cvd-host_package.tar.gz");
  std::string host_tools_filepath =
      CF_EXPECT(build_api.DownloadFile(build, target_dir, host_tools_name));
  trace.CompletePhase("Download", FileSize(host_tools_filepath));
  CF_EXPECT(
      ExtractArchiveContents(host_tools_filepath, target_dir, keep_archives));
  trace.CompletePhase("Extract");
  return {};
}

Result<std::vector<std::string>> FetchSystemImgZipImages(
    IBuildApi& build_api, const Build& build,
    const std::string& target_directory, const bool keep_downloaded_archives) {
  LOG(INFO) << "Downloading system image zip for " << build;
  const std::string system_img_zip_name = CF_EXPECT(build_api.GetBuildZipName(build, "img"));
  std::string system_img_zip = CF_EXPECTF(
      build_api.DownloadFile(build, target_directory, system_img_zip_name),
      "Unable to download {}", system_img_zip_name);
  return CF_EXPECTF(
      ExtractImages(system_img_zip, target_directory,
                    {"system.img", "product.img"}, keep_downloaded_archives),
      "Unable to extract system and product images from {}",
      system_img_zip_name);
}

Result<std::unique_ptr<CredentialSource>> GetCredentialSourceFromFlags(
      HttpClient& http_client,
      const BuildApiFlags& flags,
      const std::string& oauth_filepath) {
      return CF_EXPECT(GetCredentialSource(
          http_client, flags.credential_source, oauth_filepath,
          flags.credential_flags.use_gce_metadata,
          flags.credential_flags.credential_filepath,
          flags.credential_flags.service_account_filepath));
      }
Result<std::unique_ptr<IBuildApi>> GetBuildApi(const BuildApiFlags& flags) {
  auto resolver =
      flags.external_dns_resolver ? GetEntDnsResolve : NameResolver();
  const bool use_logging_debug_function = true;
  std::unique_ptr<HttpClient> curl =
      HttpClient::CurlClient(resolver, use_logging_debug_function);
  std::unique_ptr<HttpClient> retrying_http_client =
      HttpClient::ServerErrorRetryClient(*curl, 10,
                                         std::chrono::milliseconds(5000));
  std::unique_ptr<CredentialSource> credential_source =
      CF_EXPECT(GetCredentialSourceFromFlags(*retrying_http_client, flags,
              StringFromEnv("HOME", ".") + "/.acloud_oauth2.dat"));
  const auto cache_base_path = PerUserDir() + "/cache";
  return CreateBuildApi(std::move(retrying_http_client), std::move(curl),
                        std::move(credential_source), std::move(flags.api_key),
                        flags.wait_retry_period, std::move(flags.api_base_url),
                        std::move(flags.project_id), flags.enable_caching,
                        std::move(cache_base_path));
}

Result<LuciBuildApi> GetLuciBuildApi(const BuildApiFlags& flags) {
  auto resolver =
      flags.external_dns_resolver ? GetEntDnsResolve : NameResolver();
  const bool use_logging_debug_function = true;
  std::unique_ptr<HttpClient> curl =
      HttpClient::CurlClient(resolver, use_logging_debug_function);
  std::unique_ptr<HttpClient> retrying_http_client =
      HttpClient::ServerErrorRetryClient(*curl, 10,
                                         std::chrono::milliseconds(5000));
  std::unique_ptr<CredentialSource> luci_credential_source =
      CF_EXPECT(GetCredentialSourceFromFlags(*retrying_http_client, flags,
                   StringFromEnv("HOME", ".") + "/.config/chrome_infra/auth/tokens.json"));
  std::unique_ptr<CredentialSource> gsutil_credential_source =
      CF_EXPECT(GetCredentialSourceFromFlags(*retrying_http_client, flags,
                                StringFromEnv("HOME", ".") + "/.boto" ));

  return LuciBuildApi(std::move(retrying_http_client), std::move(curl),
                      std::move(luci_credential_source),
                      std::move(gsutil_credential_source));
}

Result<std::optional<Build>> GetBuildHelper(
    IBuildApi& build_api, const std::optional<BuildString>& build_source,
    const std::string& fallback_target) {
  if (!build_source) {
    return std::nullopt;
  }
  return CF_EXPECT(build_api.GetBuild(*build_source, fallback_target),
                   "Unable to create build from ("
                       << *build_source << ") and target (" << fallback_target
                       << ")");
}

Result<Builds> GetBuilds(IBuildApi& build_api,
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

Result<void> UpdateTargetsWithBuilds(IBuildApi& build_api,
                                     std::vector<Target>& targets) {
  for (auto& target : targets) {
    target.builds = CF_EXPECT(GetBuilds(build_api, target.build_strings));
  }
  return {};
}

Result<Build> GetHostBuild(IBuildApi& build_api, HostToolsTarget& host_target,
                           const std::optional<Build>& fallback_host_build) {
  auto host_package_build = CF_EXPECT(
      GetBuildHelper(build_api, host_target.build_string, kDefaultBuildTarget));
  CF_EXPECT(host_package_build.has_value() || fallback_host_build.has_value(),
            "Either `--host_package_build` or `--default_build` needs to be "
            "specified. Try "
            "`--default_build=aosp-main/"
            "aosp_cf_x86_64_phone-trunk_staging-userdebug`");
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

Result<void> FetchDefaultTarget(IBuildApi& build_api, const Builds& builds,
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
    std::string img_zip_name = CF_EXPECT(build_api.GetBuildZipName(*builds.default_build, "img"));
    std::string default_img_zip_filepath = CF_EXPECT(build_api.DownloadFile(
        *builds.default_build, target_directories.root, img_zip_name));
    trace.CompletePhase("Download image zip",
                        FileSize(default_img_zip_filepath));
    std::vector<std::string> image_files = CF_EXPECT(ExtractArchiveContents(
        default_img_zip_filepath, target_directories.root,
        keep_downloaded_archives));
    trace.CompletePhase("Extract image zip contents");
    LOG(DEBUG) << "Adding img-zip files for default build";
    for (auto& file : image_files) {
      LOG(VERBOSE) << file;
    }
    CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                      default_build_id, default_build_target,
                                      image_files, target_directories.root));
    DeAndroidSparse(image_files);
    trace.CompletePhase("Desparse image files");
  }

  if (builds.system || flags.download_target_files_zip) {
    LOG(INFO) << "Downloading target files zip for " << *builds.default_build;
    std::string target_files_name =
        CF_EXPECT(build_api.GetBuildZipName(*builds.default_build, "target_files"));
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

Result<void> FetchSystemTarget(IBuildApi& build_api, const Build& system_build,
                               const TargetDirectories& target_directories,
                               bool download_img_zip,
                               const bool keep_downloaded_archives,
                               FetcherConfig& config,
                               FetchTracer::Trace trace) {
  std::string target_files_name = CF_EXPECT(build_api.GetBuildZipName(system_build, "target_files"));
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
    DeAndroidSparse(system_images);
  }
  return {};
}

Result<void> FetchKernelTarget(IBuildApi& build_api, const Build& kernel_build,
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
  DeAndroidSparse({kernel_filepath});
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
    DeAndroidSparse({initramfs_img_result.value()});
  }
  return {};
}

Result<void> FetchBootTarget(IBuildApi& build_api, const Build& boot_build,
                             const std::string& target_directory,
                             const bool keep_downloaded_archives,
                             FetcherConfig& config, FetchTracer::Trace trace) {
  std::string boot_img_zip_name = CF_EXPECT(build_api.GetBuildZipName(boot_build, "img"));
  std::string downloaded_boot_filepath;
  std::optional<std::string> boot_filepath = GetFilepath(boot_build);
  if (boot_filepath) {
    downloaded_boot_filepath = CF_EXPECT(build_api.DownloadFileWithBackup(
        boot_build, target_directory, *boot_filepath, boot_img_zip_name));
  } else {
    downloaded_boot_filepath = CF_EXPECT(
        build_api.DownloadFile(boot_build, target_directory, boot_img_zip_name));
  }
  trace.CompletePhase("Download", FileSize(downloaded_boot_filepath));

  std::vector<std::string> boot_files;
  // downloaded a zip that needs to be extracted
  if (android::base::EndsWith(downloaded_boot_filepath, boot_img_zip_name)) {
    std::string extract_target = boot_filepath.value_or("boot.img");
    std::string extracted_boot = CF_EXPECT(
        ExtractImage(downloaded_boot_filepath, target_directory, extract_target));
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
  DeAndroidSparse(boot_files);
  trace.CompletePhase("Desparse");
  return {};
}

Result<void> FetchBootloaderTarget(IBuildApi& build_api,
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
  DeAndroidSparse({bootloader_filepath});
  trace.CompletePhase("Desparse image");
  return {};
}

Result<void> FetchAndroidEfiLoaderTarget(IBuildApi& build_api,
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
  DeAndroidSparse({android_efi_loader_target_filepath});
  trace.CompletePhase("Desparse image");
  return {};
}

Result<void> FetchOtaToolsTarget(IBuildApi& build_api,
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
  DeAndroidSparse(ota_tools_files);
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

Result<void> FetchTarget(IBuildApi& build_api, LuciBuildApi& luci_build_api,
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

Result<void> Fetch(const FetchFlags& flags, HostToolsTarget& host_target,
                   std::vector<Target>& targets) {
#ifdef __BIONIC__
  // TODO(schuffelen): Find a better way to deal with tzdata
  setenv("ANDROID_TZDATA_ROOT", "/", /* overwrite */ 0);
  setenv("ANDROID_ROOT", "/", /* overwrite */ 0);
#endif

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    std::unique_ptr<IBuildApi> build_api =
        CF_EXPECT(GetBuildApi(flags.build_api_flags));
    LuciBuildApi luci_build_api =
        CF_EXPECT(GetLuciBuildApi(flags.build_api_flags));
    FetchTracer tracer;
    FetchTracer::Trace prefetch_trace = tracer.NewTrace("PreFetch actions");
    CF_EXPECT(UpdateTargetsWithBuilds(*build_api, targets));
    std::optional<Build> fallback_host_build = std::nullopt;
    if (!targets.empty()) {
      fallback_host_build = targets[0].builds.default_build;
    }
    const auto host_target_build =
        CF_EXPECT(GetHostBuild(*build_api, host_target, fallback_host_build));
    prefetch_trace.CompletePhase("GetBuilds");

    auto host_package_future =
        std::async(std::launch::async, FetchHostPackage, std::ref(*build_api),
                   std::cref(host_target_build),
                   std::cref(host_target.host_tools_directory),
                   std::cref(flags.keep_downloaded_archives),
                   tracer.NewTrace("Host Package"));
    size_t count = 1;
    for (const auto& target : targets) {
      LOG(INFO) << "Starting fetch to \"" << target.directories.root << "\"";
      FetcherConfig config;
      CF_EXPECT(FetchTarget(*build_api, luci_build_api, target.builds,
                            target.directories, target.download_flags,
                            flags.keep_downloaded_archives, config, tracer));
      CF_EXPECT(SaveConfig(config, target.directories.root));
      LOG(INFO) << "Completed target fetch to '" << target.directories.root
                << "' (" << count << " out of " << targets.size() << ")";
      count++;
    }
    LOG(DEBUG) << "Waiting for host package fetch";
    CF_EXPECT(host_package_future.get());
    LOG(DEBUG) << "Performance stats:\n" << tracer.ToStyledString();
  }
  curl_global_cleanup();

  LOG(INFO) << "Completed all fetches";
  return {};
}

}  // namespace

std::string GetFetchLogsFileName(const std::string& target_directory) {
  return target_directory + "/fetch.log";
}

Result<void> FetchCvdMain(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::StderrLogger);
  const FetchFlags flags = CF_EXPECT(GetFlagValues(argc, argv));
  const bool append_subdirectory = ShouldAppendSubdirectory(flags);
  std::vector<Target> targets = GetFetchTargets(flags, append_subdirectory);
  HostToolsTarget host_target = GetHostToolsTarget(flags, append_subdirectory);
  CF_EXPECT(EnsureDirectoriesExist(flags.target_directory,
                                   host_target.host_tools_directory, targets));
  std::string log_file = GetFetchLogsFileName(flags.target_directory);

  MetadataLevel metadata_level =
      isatty(0) ? MetadataLevel::ONLY_MESSAGE : MetadataLevel::FULL;

  auto old_logger = android::base::SetLogger(
      LogToStderrAndFiles({log_file}, "", metadata_level, flags.verbosity));
  // Set the android logger to full verbosity, the tee logger will choose
  // whether to write each line.
  auto old_severity =
      android::base::SetMinimumLogSeverity(android::base::VERBOSE);

  auto fetch_res = Fetch(flags, host_target, targets);

  // This function is no longer only called direcly from a main function, so the
  // previous logger must be restored. This also ensures logs from other
  // components don't land in fetch.log.
  android::base::SetLogger(std::move(old_logger));
  android::base::SetMinimumLogSeverity(old_severity);

  CF_EXPECT(std::move(fetch_res));
  return {};
}

}  // namespace cuttlefish
