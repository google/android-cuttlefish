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

#include <stddef.h>
#include <sys/stat.h>

#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include "absl/log/log.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/archive.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/fetch/build_strings.h"
#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/commands/cvd/fetch/download_flags.h"
#include "cuttlefish/host/commands/cvd/fetch/downloaders.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_context.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_tracer.h"
#include "cuttlefish/host/commands/cvd/fetch/host_package.h"
#include "cuttlefish/host/commands/cvd/fetch/host_tools_target.h"
#include "cuttlefish/host/commands/cvd/fetch/target_directories.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/chrome_os_build_string.h"
#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"
#include "cuttlefish/host/libs/web/luci_build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/zip_string.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

bool IsSignedBuild(const Build& build) {
  if (!std::holds_alternative<DeviceBuild>(build)) {
    return false;
  }
  const auto& device_build = std::get<DeviceBuild>(build);
  return device_build.is_signed;
}

constexpr mode_t kRwxAllMode = S_IRWXU | S_IRWXG | S_IRWXO;

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
  for (size_t i = 0; i < result.size(); ++i) {
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
                                    const std::string& cache_base_path,
                                    const std::vector<Target>& targets) {
  CF_EXPECT(EnsureDirectoryExists(host_tools_directory));
  CF_EXPECT(EnsureDirectoryExists(cache_base_path));
  for (const auto& target : targets) {
    CF_EXPECT(EnsureDirectoryExists(target.directories.root, kRwxAllMode));
    CF_EXPECT(EnsureDirectoryExists(target.directories.otatools, kRwxAllMode));
    CF_EXPECT(EnsureDirectoryExists(target.directories.test_suites, kRwxAllMode));
    CF_EXPECT(EnsureDirectoryExists(target.directories.chrome_os, kRwxAllMode));
  }
  return {};
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
      .test_suites = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.test_suites_build, kDefaultBuildTarget)),
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

Result<std::string> SaveConfig(FetcherConfig& config,
                               const std::string& target_directory) {
  // Due to constraints of the build system, artifacts intentionally cannot
  // determine their own build id. So it's unclear which build number fetch_cvd
  // itself was built at.
  // https://android.googlesource.com/platform/build/+/979c9f3/Changes.md#build_number
  const std::string fetcher_path = target_directory + "/fetcher_config.json";

  CvdFile config_member = CF_EXPECT(BuildFetcherConfigMember(
      FileSource::GENERATED, "", "", fetcher_path, target_directory));
  CF_EXPECT(config.add_cvd_file(std::move(config_member)));

  config.SaveToFile(fetcher_path);

  for (const auto& file : config.get_cvd_files()) {
    VLOG(1) << target_directory << "/" << file.second.file_path << "\n";
  }
  return fetcher_path;
}

Result<void> FetchDefaultTarget(FetchBuildContext& context,
                                bool keep_downloaded_archives,
                                const DownloadFlags& flags,
                                bool has_system_build) {
  constexpr char kSignedPrefix[] = "signed/signed-";
  // Some older builds might not have misc_info.txt, so permit errors on
  // fetching misc_info.txt
  if (!context.Artifact("misc_info.txt").Download().ok()) {
    VLOG(0) << "Failed to download misc_info.txt, continuing";
  }
  if (flags.download_img_zip) {
    LOG(INFO) << "Downloading image zip for " << context;
    std::string img_zip_name = context.GetBuildZipName("img");
    std::string img_zip_artifact_name = img_zip_name;
    if (IsSignedBuild(context.Build())) {
      img_zip_artifact_name = kSignedPrefix + img_zip_artifact_name;
      LOG(INFO) << "Attempting to fetch SIGNED default image zip: "
                  << img_zip_artifact_name;
    }
    FetchArtifact img_zip = context.Artifact(img_zip_artifact_name);
    CF_EXPECT(img_zip.Download());
    CF_EXPECT(img_zip.ExtractAll());
    if (!keep_downloaded_archives) {
      CF_EXPECT(img_zip.DeleteLocalFile());
    }
  }
  std::string target_files_name = context.GetBuildZipName("target_files");
  FetchArtifact target_files = context.Artifact(target_files_name);
  if (has_system_build || flags.download_target_files_zip) {
    LOG(INFO) << "Downloading target files zip for " << context;
    std::string download_location =
        fmt::format("default/{}", target_files_name);
    CF_EXPECT(target_files.DownloadTo(download_location));
  }
  if (flags.dynamic_super_image) {
    ReadableZip* target_files_zip = CF_EXPECT(target_files.AsZip());
    ReadableZipSource ab_partitions_source =
        CF_EXPECT(target_files_zip->GetFile("META/ab_partitions.txt"));
    std::string ab_partitions_contents =
        CF_EXPECT(ReadToString(ab_partitions_source));

    CF_EXPECT(target_files.ExtractOneTo("META/ab_partitions.txt",
                                        "default/ab_partitions.txt"));

    std::vector<std::string_view> ab_files =
        absl::StrSplit(ab_partitions_contents, '\n');
    ab_files.emplace_back("super_empty");
    for (std::string_view ab_file : ab_files) {
      if (ab_file.empty()) {
        continue;
      }
      std::string member = fmt::format("IMAGES/{}.img", ab_file);
      std::string output = fmt::format("default/{}.img", ab_file);
      CF_EXPECT(target_files.ExtractOneTo(member, output));
    }
  }
  return {};
}

Result<void> FetchSystemTarget(FetchBuildContext& context,
                               bool download_img_zip,
                               const bool keep_downloaded_archives) {
  std::string target_files_name = context.GetBuildZipName("target_files");
  FetchArtifact target_files = context.Artifact(target_files_name);

  CF_EXPECT(
      target_files.DownloadTo(fmt::format("system/{}", target_files_name)));

  if (download_img_zip) {
    LOG(INFO) << "Downloading system image zip for " << context;
    if (!target_files.ExtractOneTo("IMAGES/system.img", "system.img").ok()) {
      LOG(INFO) << "Unable to retrieve system.img from target files, falling "
                   "back to system *-img-*.zip for system image";
      std::string system_img_zip_name = context.GetBuildZipName("img");
      FetchArtifact system_files = context.Artifact(system_img_zip_name);

      CF_EXPECT(system_files.Download());
      CF_EXPECT(system_files.ExtractOne("system.img"));
      CF_EXPECT(system_files.ExtractOne("product.img"));

      if (!keep_downloaded_archives) {
        CF_EXPECT(system_files.DeleteLocalFile());
      }
    }

    static constexpr std::string_view kSystemImageFiles[] = {
        "init_boot",
        "product",
        "system_ext",
        "vbmeta_system",
    };
    for (std::string_view system_image : kSystemImageFiles) {
      std::string member = fmt::format("IMAGES/{}.img", system_image);
      std::string rename_to = fmt::format("{}.img", system_image);
      if (!target_files.ExtractOneTo(member, rename_to).ok()) {
        VLOG(0) << "Failed to extract " << member;
      }
    }
  }
  return {};
}

Result<void> FetchKernelTarget(FetchBuildContext context) {
  // If the kernel is from an arm/aarch64 build, the artifact will be called
  // Image.
  if (!context.Artifact("bzImage").DownloadTo("kernel").ok()) {
    CF_EXPECT(context.Artifact("Image").DownloadTo("kernel"));
  }

  // Certain kernel builds do not have corresponding ramdisks.
  if (!context.Artifact("initramfs.img").Download().ok()) {
    VLOG(0) << "No initramfs.img for kernel build, ignoring";
  }
  return {};
}

Result<void> FetchBootTarget(FetchBuildContext& context,
                             bool keep_downloaded_archives) {
  std::string img_zip = context.GetBuildZipName("img");
  std::string to_download = context.GetFilepath().value_or(img_zip);
  FetchArtifact artifact = context.Artifact(to_download);
  CF_EXPECT(artifact.Download());

  if (to_download == img_zip) {
    CF_EXPECT(artifact.ExtractOne("boot.img"));
    CF_EXPECT(artifact.ExtractOne("vendor_boot.img"));
    if (!keep_downloaded_archives) {
      CF_EXPECT(artifact.DeleteLocalFile());
    }
  }

  return {};
}

Result<void> FetchBootloaderTarget(FetchBuildContext& context) {
  // If the bootloader is from an arm/aarch64 build, the artifact will be of
  // filetype bin.
  if (!context.Artifact("u-boot.rom").DownloadTo("bootloader").ok()) {
    CF_EXPECT(context.Artifact("u-boot.bin").DownloadTo("bootloader"));
  }
  return {};
}

Result<void> FetchAndroidEfiLoaderTarget(FetchBuildContext& context) {
  std::string filename = context.GetFilepath().value_or("gbl_x86_64.efi");
  CF_EXPECT(context.Artifact(filename).DownloadTo("android_efi_loader.efi"));
  return {};
}

Result<void> FetchOtaToolsTarget(FetchBuildContext& context,
                                 bool keep_downloaded_archives) {
  FetchArtifact otatools = context.Artifact("otatools.zip");
  CF_EXPECT(otatools.Download());
  CF_EXPECT(otatools.ExtractAll());
  if (!keep_downloaded_archives) {
    CF_EXPECT(otatools.DeleteLocalFile());
  }
  return {};
}

Result<void> FetchTestSuitesTarget(FetchBuildContext& context,
                                   bool keep_downloaded_archives) {
  FetchArtifact android_cts = context.Artifact("android-cts.zip");
  // TODO(b/468074996): determine what tradefed actually needs and potentially
  // expose a flag to allow downloading specific parts of the entire zip.
  CF_EXPECT(android_cts.Download());
  CF_EXPECT(android_cts.ExtractAll());
  if (!keep_downloaded_archives) {
    CF_EXPECT(android_cts.DeleteLocalFile());
  }
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
  std::vector<std::string> archive_files = CF_EXPECT(ExtractArchiveContents(
      archive_path, target_directories.chrome_os, keep_downloaded_archives));
  trace.CompletePhase("Extract");
  for (std::string& archive_file : archive_files) {
    CvdFile config_member = CF_EXPECT(BuildFetcherConfigMember(
        FileSource::CHROME_OS_BUILD, "", "", std::move(archive_file),
        target_directories.root));
    CF_EXPECT(config.add_cvd_file(std::move(config_member)));
  }
  return {};
}

Result<void> FetchTarget(FetchContext& fetch_context,
                         const DownloadFlags& flags,
                         const bool keep_downloaded_archives) {
  if (std::optional<FetchBuildContext> context = fetch_context.DefaultBuild()) {
    bool has_system_build = fetch_context.SystemBuild().has_value();
    CF_EXPECT(FetchDefaultTarget(*context, keep_downloaded_archives, flags,
                                 has_system_build));
  }

  if (std::optional<FetchBuildContext> context = fetch_context.SystemBuild()) {
    CF_EXPECT(FetchSystemTarget(*context, flags.download_img_zip,
                                keep_downloaded_archives));
  }

  if (std::optional<FetchBuildContext> context = fetch_context.KernelBuild()) {
    CF_EXPECT(FetchKernelTarget(*context));
  }

  if (std::optional<FetchBuildContext> context = fetch_context.BootBuild()) {
    CF_EXPECT(FetchBootTarget(*context, keep_downloaded_archives));
  }

  if (std::optional<FetchBuildContext> ctx = fetch_context.BootloaderBuild()) {
    CF_EXPECT(FetchBootloaderTarget(*ctx));
  }

  if (std::optional<FetchBuildContext> ctx =
          fetch_context.AndroidEfiLoaderBuild()) {
    CF_EXPECT(FetchAndroidEfiLoaderTarget(*ctx));
  }

  if (std::optional<FetchBuildContext> ctx = fetch_context.OtaToolsBuild()) {
    CF_EXPECT(FetchOtaToolsTarget(*ctx, keep_downloaded_archives));
  }

  if (std::optional<FetchBuildContext> ctx = fetch_context.TestSuitesBuild()) {
    CF_EXPECT(FetchTestSuitesTarget(*ctx, keep_downloaded_archives));
  }

  return {};
}

Result<std::vector<FetchResult>> Fetch(const FetchFlags& flags,
                                       const std::string& cache_base_path,
                                       const HostToolsTarget& host_target,
                                       std::vector<Target>& targets) {
#ifdef __BIONIC__
  // TODO(schuffelen): Find a better way to deal with tzdata
  setenv("ANDROID_TZDATA_ROOT", "/", /* overwrite */ 0);
  setenv("ANDROID_ROOT", "/", /* overwrite */ 0);
#endif
  CurlGlobalInit curl_init;

  Downloaders downloaders = CF_EXPECT(Downloaders::Create(
      flags.build_api_flags, flags.target_directory, cache_base_path));

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
  std::vector<FetchResult> fetch_results;
  for (const auto& target : targets) {
    FetcherConfig config;
    FetchContext fetch_context(downloaders.AndroidBuild(), target.directories,
                               target.builds, config, tracer);
    LOG(INFO) << "Starting fetch to \"" << target.directories.root << "\"";
    CF_EXPECT(FetchTarget(fetch_context, target.download_flags,
                          flags.keep_downloaded_archives));

    if (target.builds.chrome_os) {
      CF_EXPECT(FetchChromeOsTarget(
          downloaders.Luci(), *target.builds.chrome_os, target.directories,
          flags.keep_downloaded_archives, config, tracer.NewTrace("ChromeOS")));
    }

    const std::string config_path =
        CF_EXPECT(SaveConfig(config, target.directories.root));
    fetch_results.emplace_back(FetchResult{
        .fetcher_config_path = config_path,
        .builds = target.builds,
    });
    LOG(INFO) << "Completed target fetch to '" << target.directories.root
              << "' (" << count << " out of " << targets.size() << ")";
    count++;
  }
  VLOG(0) << "Waiting for host package fetch";
  CF_EXPECT(host_package_future.get());
  VLOG(0) << "Performance stats:\n" << tracer.ToStyledString();

  LOG(INFO) << "Completed all fetches";
  return fetch_results;
}

}  // namespace

std::string GetFetchLogsFileName(const std::string& target_directory) {
  return target_directory + "/fetch.log";
}

Result<std::vector<FetchResult>> FetchCvdMain(const FetchFlags& flags) {
  const bool append_subdirectory = ShouldAppendSubdirectory(flags);
  std::vector<Target> targets = GetFetchTargets(flags, append_subdirectory);
  HostToolsTarget host_target =
      HostToolsTarget::Create(flags, append_subdirectory);
  const std::string cache_base_path = PerUserCacheDir();
  CF_EXPECT(EnsureDirectoriesExist(host_target.host_tools_directory,
                                   cache_base_path, targets));
  return CF_EXPECT(Fetch(flags, cache_base_path, host_target, targets));
}

}  // namespace cuttlefish
