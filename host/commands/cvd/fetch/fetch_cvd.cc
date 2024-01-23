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
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <curl/curl.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/build_string.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

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
  std::optional<BuildString> otatools_build;
  std::optional<BuildString> host_package_build;
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
};

struct Builds {
  std::optional<Build> default_build;
  std::optional<Build> system;
  std::optional<Build> kernel;
  std::optional<Build> boot;
  std::optional<Build> bootloader;
  std::optional<Build> otatools;
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
T AccessOrDefault(const std::vector<T>& vector, const int i,
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
      .otatools_build = AccessOrDefault<std::optional<BuildString>>(
          flags.otatools_build, index, std::nullopt),
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
                           .system_target_files = base_directory + "/system"};
}

std::vector<Target> GetFetchTargets(const FetchFlags& flags,
                                    const bool append_subdirectory) {
  std::vector<Target> result(flags.number_of_builds);
  for (int i = 0; i < result.size(); ++i) {
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
  }
  return {};
}

Result<void> FetchHostPackage(BuildApi& build_api, const Build& build,
                              const std::string& target_dir,
                              const bool keep_archives) {
  std::string host_tools_filepath = CF_EXPECT(
      build_api.DownloadFile(build, target_dir, "cvd-host_package.tar.gz"));
  CF_EXPECT(
      ExtractArchiveContents(host_tools_filepath, target_dir, keep_archives));
  return {};
}

Result<BuildApi> GetBuildApi(const BuildApiFlags& flags) {
  auto resolver =
      flags.external_dns_resolver ? GetEntDnsResolve : NameResolver();
  const bool use_logging_debug_function = true;
  std::unique_ptr<HttpClient> curl =
      HttpClient::CurlClient(resolver, use_logging_debug_function);
  std::unique_ptr<HttpClient> retrying_http_client =
      HttpClient::ServerErrorRetryClient(*curl, 10,
                                         std::chrono::milliseconds(5000));
  std::string oauth_filepath =
      StringFromEnv("HOME", ".") + "/.acloud_oauth2.dat";
  std::unique_ptr<CredentialSource> credential_source =
      CF_EXPECT(GetCredentialSource(
          *retrying_http_client, flags.credential_source, oauth_filepath,
          flags.credential_flags.use_gce_metadata,
          flags.credential_flags.credential_filepath,
          flags.credential_flags.service_account_filepath));

  return BuildApi(std::move(retrying_http_client), std::move(curl),
                  std::move(credential_source), flags.api_key,
                  flags.wait_retry_period, flags.api_base_url);
}

Result<std::optional<Build>> GetBuildHelper(
    BuildApi& build_api, const std::optional<BuildString>& build_source,
    const std::string& fallback_target) {
  if (!build_source) {
    return std::nullopt;
  }
  return CF_EXPECT(build_api.GetBuild(*build_source, fallback_target),
                   "Unable to create build from ("
                       << *build_source << ") and target (" << fallback_target
                       << ")");
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
      .otatools = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.otatools_build, kDefaultBuildTarget)),
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

Result<Build> GetHostBuild(BuildApi& build_api, HostToolsTarget& host_target,
                           const std::optional<Build>& fallback_host_build) {
  auto host_package_build = CF_EXPECT(
      GetBuildHelper(build_api, host_target.build_string, kDefaultBuildTarget));
  CF_EXPECT(host_package_build.has_value() || fallback_host_build.has_value(),
            "Either the host_package_build or default_build requires a value. "
            "(previous default_build default was "
            "aosp-master/aosp_cf_x86_64_phone-userdebug)");
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

Result<void> FetchTarget(BuildApi& build_api, const Builds& builds,
                         const TargetDirectories& target_directories,
                         const DownloadFlags& flags,
                         const bool keep_downloaded_archives,
                         FetcherConfig& config) {
  if (builds.default_build) {
    const auto [default_build_id, default_build_target] =
        GetBuildIdAndTarget(*builds.default_build);

    // Some older builds might not have misc_info.txt, so permit errors on
    // fetching misc_info.txt
    Result<std::string> misc_info_result = build_api.DownloadFile(
        *builds.default_build, target_directories.root, "misc_info.txt");
    if (misc_info_result.ok()) {
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::DEFAULT_BUILD, default_build_id, default_build_target,
          {misc_info_result.value()}, target_directories.root,
          kOverrideEntries));
    }

    if (flags.download_img_zip) {
      std::string img_zip_name = GetBuildZipName(*builds.default_build, "img");
      std::string default_img_zip_filepath = CF_EXPECT(build_api.DownloadFile(
          *builds.default_build, target_directories.root, img_zip_name));
      std::vector<std::string> image_files = CF_EXPECT(ExtractArchiveContents(
          default_img_zip_filepath, target_directories.root,
          keep_downloaded_archives));
      LOG(INFO) << "Adding img-zip files for default build";
      for (auto& file : image_files) {
        LOG(VERBOSE) << file;
      }
      CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                        default_build_id, default_build_target,
                                        image_files, target_directories.root));
    }

    if (builds.system || flags.download_target_files_zip) {
      std::string target_files_name =
          GetBuildZipName(*builds.default_build, "target_files");
      std::string target_files = CF_EXPECT(build_api.DownloadFile(
          *builds.default_build, target_directories.default_target_files,
          target_files_name));
      LOG(INFO) << "Adding target files for default build";
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::DEFAULT_BUILD, default_build_id, default_build_target,
          {target_files}, target_directories.root));
    }
  }

  if (builds.system) {
    std::string target_files_name =
        GetBuildZipName(*builds.system, "target_files");
    std::string target_files = CF_EXPECT(build_api.DownloadFile(
        *builds.system, target_directories.system_target_files,
        target_files_name));
    const auto [system_id, system_target] = GetBuildIdAndTarget(*builds.system);
    CF_EXPECT(config.AddFilesToConfig(FileSource::SYSTEM_BUILD, system_id,
                                      system_target, {target_files},
                                      target_directories.root));

    if (flags.download_img_zip) {
      std::string system_img_zip_name = GetBuildZipName(*builds.system, "img");
      Result<std::string> system_img_zip_result = build_api.DownloadFile(
          *builds.system, target_directories.root, system_img_zip_name);
      Result<std::vector<std::string>> extract_result;
      if (system_img_zip_result.ok()) {
        extract_result = ExtractImages(
            system_img_zip_result.value(), target_directories.root,
            {"system.img", "product.img"}, keep_downloaded_archives);
        if (extract_result.ok()) {
          CF_EXPECT(config.AddFilesToConfig(
              FileSource::SYSTEM_BUILD, system_id, system_target,
              extract_result.value(), target_directories.root,
              kOverrideEntries));
        }
      }
      if (!system_img_zip_result.ok() || !extract_result.ok()) {
        std::string extracted_system = CF_EXPECT(ExtractImage(
            target_files, target_directories.root, "IMAGES/system.img"));
        CF_EXPECT(RenameFile(extracted_system,
                             target_directories.root + "/system.img"));

        Result<std::string> extracted_product_result = ExtractImage(
            target_files, target_directories.root, "IMAGES/product.img");
        if (extracted_product_result.ok()) {
          CF_EXPECT(RenameFile(extracted_product_result.value(),
                               target_directories.root + "/product.img"));
        }

        Result<std::string> extracted_system_ext_result = ExtractImage(
            target_files, target_directories.root, "IMAGES/system_ext.img");
        if (extracted_system_ext_result.ok()) {
          CF_EXPECT(RenameFile(extracted_system_ext_result.value(),
                               target_directories.root + "/system_ext.img"));
        }

        Result<std::string> extracted_vbmeta_system = ExtractImage(
            target_files, target_directories.root, "IMAGES/vbmeta_system.img");
        if (extracted_vbmeta_system.ok()) {
          CF_EXPECT(RenameFile(extracted_vbmeta_system.value(),
                               target_directories.root + "/vbmeta_system.img"));
        }
        Result<std::string> extracted_init_boot = ExtractImage(
            target_files, target_directories.root, "IMAGES/init_boot.img");
        if (extracted_init_boot.ok()) {
          CF_EXPECT(RenameFile(extracted_init_boot.value(),
                               target_directories.root + "/init_boot.img"));
        }
      }
    }
  }

  if (builds.kernel) {
    std::string kernel_filepath = target_directories.root + "/kernel";
    // If the kernel is from an arm/aarch64 build, the artifact will be called
    // Image.
    std::string downloaded_kernel_filepath =
        CF_EXPECT(build_api.DownloadFileWithBackup(
            *builds.kernel, target_directories.root, "bzImage", "Image"));
    CF_EXPECT(RenameFile(downloaded_kernel_filepath, kernel_filepath));
    const auto [kernel_id, kernel_target] = GetBuildIdAndTarget(*builds.kernel);
    CF_EXPECT(config.AddFilesToConfig(FileSource::KERNEL_BUILD, kernel_id,
                                      kernel_target, {kernel_filepath},
                                      target_directories.root));

    // Certain kernel builds do not have corresponding ramdisks.
    Result<std::string> initramfs_img_result = build_api.DownloadFile(
        *builds.kernel, target_directories.root, "initramfs.img");
    if (initramfs_img_result.ok()) {
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::KERNEL_BUILD, kernel_id, kernel_target,
          {initramfs_img_result.value()}, target_directories.root));
    }
  }

  if (builds.boot) {
    std::string boot_img_zip_name = GetBuildZipName(*builds.boot, "img");
    std::string downloaded_boot_filepath;
    std::optional<std::string> boot_filepath = GetFilepath(*builds.boot);
    if (boot_filepath) {
      downloaded_boot_filepath = CF_EXPECT(build_api.DownloadFileWithBackup(
          *builds.boot, target_directories.root, *boot_filepath,
          boot_img_zip_name));
    } else {
      downloaded_boot_filepath = CF_EXPECT(build_api.DownloadFile(
          *builds.boot, target_directories.root, boot_img_zip_name));
    }

    std::vector<std::string> boot_files;
    // downloaded a zip that needs to be extracted
    if (android::base::EndsWith(downloaded_boot_filepath, boot_img_zip_name)) {
      std::string extract_target = boot_filepath.value_or("boot.img");
      std::string extracted_boot = CF_EXPECT(ExtractImage(
          downloaded_boot_filepath, target_directories.root, extract_target));
      std::string target_boot = CF_EXPECT(
          RenameFile(extracted_boot, target_directories.root + "/boot.img"));
      boot_files.push_back(target_boot);

      // keep_downloaded_archives flag used because this is the last extract
      // on this archive
      Result<std::string> extracted_vendor_boot_result =
          ExtractImage(downloaded_boot_filepath, target_directories.root,
                       "vendor_boot.img", keep_downloaded_archives);
      if (extracted_vendor_boot_result.ok()) {
        boot_files.push_back(extracted_vendor_boot_result.value());
      }
    } else {
      boot_files.push_back(downloaded_boot_filepath);
    }
    const auto [boot_id, boot_target] = GetBuildIdAndTarget(*builds.boot);
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::BOOT_BUILD, boot_id, boot_target, boot_files,
        target_directories.root, kOverrideEntries));
  }

  if (builds.bootloader) {
    std::string bootloader_filepath = target_directories.root + "/bootloader";
    // If the bootloader is from an arm/aarch64 build, the artifact will be of
    // filetype bin.
    std::string downloaded_bootloader_filepath =
        CF_EXPECT(build_api.DownloadFileWithBackup(*builds.bootloader,
                                                   target_directories.root,
                                                   "u-boot.rom", "u-boot.bin"));
    CF_EXPECT(RenameFile(downloaded_bootloader_filepath, bootloader_filepath));
    const auto [bootloader_id, bootloader_target] =
        GetBuildIdAndTarget(*builds.bootloader);
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::BOOTLOADER_BUILD, bootloader_id, bootloader_target,
        {bootloader_filepath}, target_directories.root, kOverrideEntries));
  }

  if (builds.otatools) {
    std::string otatools_filepath = CF_EXPECT(build_api.DownloadFile(
        *builds.otatools, target_directories.root, "ota_tools.zip"));
    std::vector<std::string> ota_tools_files = CF_EXPECT(
        ExtractArchiveContents(otatools_filepath, target_directories.otatools,
                               keep_downloaded_archives));
    const auto [otatools_build_id, otatools_build_target] =
        GetBuildIdAndTarget(*builds.otatools);
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::DEFAULT_BUILD, otatools_build_id, otatools_build_target,
        ota_tools_files, target_directories.root));
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
    BuildApi build_api = CF_EXPECT(GetBuildApi(flags.build_api_flags));
    CF_EXPECT(UpdateTargetsWithBuilds(build_api, targets));
    std::optional<Build> fallback_host_build = std::nullopt;
    if (!targets.empty()) {
      fallback_host_build = targets[0].builds.default_build;
    }
    const auto host_target_build =
        CF_EXPECT(GetHostBuild(build_api, host_target, fallback_host_build));

    auto host_package_future =
        std::async(std::launch::async, FetchHostPackage, std::ref(build_api),
                   std::cref(host_target_build),
                   std::cref(host_target.host_tools_directory),
                   std::cref(flags.keep_downloaded_archives));
    for (const auto& target : targets) {
      LOG(INFO) << "Starting fetch to \"" << target.directories.root << "\"";
      FetcherConfig config;
      CF_EXPECT(FetchTarget(build_api, target.builds, target.directories,
                            target.download_flags,
                            flags.keep_downloaded_archives, config));
      CF_EXPECT(SaveConfig(config, target.directories.root));
      LOG(INFO) << "Completed fetch to \"" << target.directories.root << "\"";
    }
    CF_EXPECT(host_package_future.get());
  }
  curl_global_cleanup();

  LOG(INFO) << "Completed all fetches";
  return {};
}

}  // namespace

Result<void> FetchCvdMain(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::StderrLogger);
  const FetchFlags flags = CF_EXPECT(GetFlagValues(argc, argv));
  const bool append_subdirectory = ShouldAppendSubdirectory(flags);
  std::vector<Target> targets = GetFetchTargets(flags, append_subdirectory);
  HostToolsTarget host_target = GetHostToolsTarget(flags, append_subdirectory);
  CF_EXPECT(EnsureDirectoriesExist(flags.target_directory,
                                   host_target.host_tools_directory, targets));
  android::base::SetLogger(
      LogToStderrAndFiles({flags.target_directory + "/fetch.log"}));
  android::base::SetMinimumLogSeverity(flags.verbosity);

  auto result = Fetch(flags, host_target, targets);
  if (!result.ok()) {
    LOG(ERROR) << result.error().FormatForEnv();
  }
  return result;
}

}  // namespace cuttlefish
