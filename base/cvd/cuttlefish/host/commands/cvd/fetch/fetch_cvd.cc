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
#include <thread>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <curl/curl.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/credential_source.h"

namespace cuttlefish {
namespace {

const std::string DEFAULT_BRANCH = "aosp-master";
const std::string DEFAULT_BUILD_TARGET = "aosp_cf_x86_64_phone-userdebug";
const std::string HOST_TOOLS = "cvd-host_package.tar.gz";
const std::string KERNEL = "kernel";
const std::string OTA_TOOLS = "otatools.zip";
const std::string OTA_TOOLS_DIR = "/otatools/";
const std::string DEFAULT_DIR = "/default";
const std::string SYSTEM_DIR = "/system";
const int DEFAULT_RETRY_PERIOD = 20;
const std::string USAGE_MESSAGE =
    "<flags>\n"
    "\n"
    "\"*_build\" flags accept values in the following format:\n"
    "\"branch/build_target\" - latest build of \"branch\" for "
    "\"build_target\"\n"
    "\"build_id/build_target\" - build \"build_id\" for \"build_target\"\n"
    "\"branch\" - latest build of \"branch\" for "
    "\"aosp_cf_x86_phone-userdebug\"\n"
    "\"build_id\" - build \"build_id\" for \"aosp_cf_x86_phone-userdebug\"\n";
const mode_t RWX_ALL_MODE = S_IRWXU | S_IRWXG | S_IRWXO;
const bool OVERRIDE_ENTRIES = true;
const bool DOWNLOAD_IMG_ZIP_DEFAULT = true;
const bool DOWNLOAD_TARGET_FILES_ZIP_DEFAULT = false;

struct BuildApiFlags {
  std::string api_key = "";
  std::string credential_source = "";
  std::chrono::seconds wait_retry_period =
      std::chrono::seconds(DEFAULT_RETRY_PERIOD);
  bool external_dns_resolver =
#ifdef __BIONIC__
      true;
#else
      false;
#endif
};

struct VectorFlags {
  std::vector<std::string> default_build;
  std::vector<std::string> system_build;
  std::vector<std::string> kernel_build;
  std::vector<std::string> boot_build;
  std::vector<std::string> bootloader_build;
  std::vector<std::string> otatools_build;
  std::vector<std::string> host_package_build;
  std::vector<std::string> boot_artifact;
  std::vector<bool> download_img_zip;
  std::vector<bool> download_target_files_zip;
};

struct BuildSourceFlags {
  std::string default_build;
  std::string system_build;
  std::string kernel_build;
  std::string boot_build;
  std::string bootloader_build;
  std::string otatools_build;
  std::string host_package_build;
};

struct DownloadFlags {
  std::string boot_artifact;
  bool download_img_zip;
  bool download_target_files_zip;
};

struct FetchFlags {
  std::string target_directory = "";
  std::vector<std::string> target_subdirectory;
  bool keep_downloaded_archives = false;
  bool helpxml = false;
  BuildApiFlags build_api_flags;
  std::vector<std::tuple<BuildSourceFlags, DownloadFlags, int>>
      build_target_flags;
};

struct Builds {
  Build default_build;
  std::optional<Build> system;
  std::optional<Build> kernel;
  std::optional<Build> boot;
  std::optional<Build> bootloader;
  std::optional<Build> otatools;
  Build host_package;
};

struct TargetDirectories {
  std::string root;
  std::string otatools;
  std::string default_target_files;
  std::string system_target_files;
};

std::vector<Flag> GetFlagsVector(FetchFlags& fetch_flags,
                                 BuildApiFlags& build_api_flags,
                                 VectorFlags& vector_flags, int& retry_period,
                                 std::string& directory) {
  std::vector<Flag> flags;
  flags.emplace_back(
      GflagsCompatFlag("directory", directory)
          .Help("Target directory to fetch files into. (deprecated)"));
  flags.emplace_back(
      GflagsCompatFlag("target_directory", fetch_flags.target_directory)
          .Help("Target directory to fetch files into."));
  flags.emplace_back(GflagsCompatFlag("keep_downloaded_archives",
                                      fetch_flags.keep_downloaded_archives)
                         .Help("Keep downloaded zip/tar."));
  flags.emplace_back(
      GflagsCompatFlag("target_subdirectory", fetch_flags.target_subdirectory)
          .Help("Target subdirectory to fetch files into.  Specifically aimed "
                "at organizing builds when there are multiple fetches. "
                "**Note**: directory separator automatically prepended, only "
                "give the subdirectory name."));

  flags.emplace_back(GflagsCompatFlag("api_key", build_api_flags.api_key)
                         .Help("API key ofr the Android Build API"));
  flags.emplace_back(
      GflagsCompatFlag("credential_source", build_api_flags.credential_source)
          .Help("Build API credential source"));
  flags.emplace_back(GflagsCompatFlag("wait_retry_period", retry_period)
                         .Help("Retry period for pending builds given in "
                               "seconds. Set to 0 to not wait."));
  flags.emplace_back(
      GflagsCompatFlag("external_dns_resolver",
                       build_api_flags.external_dns_resolver)
          .Help("Use an out-of-process mechanism to resolve DNS queries"));

  flags.emplace_back(
      GflagsCompatFlag("default_build", vector_flags.default_build)
          .Help("source for the cuttlefish build to use (vendor.img + host)"));
  flags.emplace_back(GflagsCompatFlag("system_build", vector_flags.system_build)
                         .Help("source for system.img and product.img"));
  flags.emplace_back(GflagsCompatFlag("kernel_build", vector_flags.kernel_build)
                         .Help("source for the kernel or gki target"));
  flags.emplace_back(GflagsCompatFlag("boot_build", vector_flags.boot_build)
                         .Help("source for the boot or gki target"));
  flags.emplace_back(
      GflagsCompatFlag("bootloader_build", vector_flags.bootloader_build)
          .Help("source for the bootloader target"));
  flags.emplace_back(
      GflagsCompatFlag("otatools_build", vector_flags.otatools_build)
          .Help("source for the host ota tools"));
  flags.emplace_back(
      GflagsCompatFlag("host_package_build", vector_flags.host_package_build)
          .Help("source for the host cvd tools"));

  flags.emplace_back(
      GflagsCompatFlag("boot_artifact", vector_flags.boot_artifact)
          .Help("name of the boot image in boot_build"));
  flags.emplace_back(GflagsCompatFlag("download_img_zip",
                                      vector_flags.download_img_zip,
                                      DOWNLOAD_IMG_ZIP_DEFAULT)
                         .Help("Whether to fetch the -img-*.zip file."));
  flags.emplace_back(
      GflagsCompatFlag("download_target_files_zip",
                       vector_flags.download_target_files_zip,
                       DOWNLOAD_TARGET_FILES_ZIP_DEFAULT)
          .Help("Whether to fetch the -target_files-*.zip file."));

  flags.emplace_back(HelpFlag(flags, USAGE_MESSAGE));
  flags.emplace_back(
      HelpXmlFlag(flags, std::cout, fetch_flags.helpxml, USAGE_MESSAGE));

  flags.emplace_back(UnexpectedArgumentGuard());
  return flags;
}

Result<int> GetNumberOfBuilds(
    const VectorFlags& flags,
    const std::vector<std::string>& subdirectory_flag) {
  std::optional<int> number_of_builds;
  for (const auto& flag_size :
       {flags.default_build.size(), flags.system_build.size(),
        flags.kernel_build.size(), flags.boot_build.size(),
        flags.bootloader_build.size(), flags.otatools_build.size(),
        flags.host_package_build.size(), flags.boot_artifact.size(),
        flags.download_img_zip.size(), flags.download_target_files_zip.size(),
        subdirectory_flag.size()}) {
    if (flag_size == 0) {
      // a size zero flag vector means the flag was not given
      continue;
    }
    if (number_of_builds) {
      CF_EXPECT(
          flag_size == *number_of_builds,
          "Mismatched flag lengths: " << *number_of_builds << "," << flag_size);
    }
    number_of_builds = flag_size;
  }
  // if no flags had values there is 1 all-default build
  return number_of_builds.value_or(1);
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

// Maps existing vectors of flags to the flag collections used for each build's
// fetch, providing default values for flags that were not provided
Result<std::vector<std::tuple<BuildSourceFlags, DownloadFlags, int>>>
MapToBuildTargetFlags(const VectorFlags& flags, const int num_builds) {
  std::vector<std::tuple<BuildSourceFlags, DownloadFlags, int>> result(
      num_builds);
  for (int i = 0; i < result.size(); ++i) {
    auto build_source = BuildSourceFlags{
        .default_build = AccessOrDefault<std::string>(
            flags.default_build, i,
            DEFAULT_BRANCH + "/" + DEFAULT_BUILD_TARGET),
        .system_build = AccessOrDefault<std::string>(flags.system_build, i, ""),
        .kernel_build = AccessOrDefault<std::string>(flags.kernel_build, i, ""),
        .boot_build = AccessOrDefault<std::string>(flags.boot_build, i, ""),
        .bootloader_build =
            AccessOrDefault<std::string>(flags.bootloader_build, i, ""),
        .otatools_build =
            AccessOrDefault<std::string>(flags.otatools_build, i, ""),
        .host_package_build =
            AccessOrDefault<std::string>(flags.host_package_build, i, ""),
    };
    auto download = DownloadFlags{
        .boot_artifact =
            AccessOrDefault<std::string>(flags.boot_artifact, i, ""),
        .download_img_zip = AccessOrDefault<bool>(flags.download_img_zip, i,
                                                  DOWNLOAD_IMG_ZIP_DEFAULT),
        .download_target_files_zip =
            AccessOrDefault<bool>(flags.download_target_files_zip, i,
                                  DOWNLOAD_TARGET_FILES_ZIP_DEFAULT),
    };
    result[i] = {build_source, download, i};
  }
  return result;
}

Result<FetchFlags> GetFlagValues(int argc, char** argv) {
  FetchFlags fetch_flags;
  BuildApiFlags build_api_flags;
  VectorFlags vector_flags;
  int retry_period = DEFAULT_RETRY_PERIOD;
  std::string directory = "";

  std::vector<Flag> flags = GetFlagsVector(
      fetch_flags, build_api_flags, vector_flags, retry_period, directory);
  std::vector<std::string> args = ArgsToVec(argc - 1, argv + 1);
  CF_EXPECT(ParseFlags(flags, args), "Could not process command line flags.");

  build_api_flags.wait_retry_period = std::chrono::seconds(retry_period);
  if (directory != "") {
    LOG(ERROR) << "Please use --target_directory instead of --directory";
    if (fetch_flags.target_directory == "") {
      fetch_flags.target_directory = directory;
    }
  } else {
    if (fetch_flags.target_directory == "") {
      fetch_flags.target_directory = CurrentDirectory();
    }
  }

  fetch_flags.build_api_flags = build_api_flags;
  const int num_builds = CF_EXPECT(
      GetNumberOfBuilds(vector_flags, fetch_flags.target_subdirectory));
  fetch_flags.build_target_flags =
      CF_EXPECT(MapToBuildTargetFlags(vector_flags, num_builds));
  return {fetch_flags};
}

std::unique_ptr<CredentialSource> TryParseServiceAccount(
    HttpClient& http_client, const std::string& file_content) {
  Json::Reader reader;
  Json::Value content;
  if (!reader.parse(file_content, content)) {
    // Don't log the actual content of the file since it could be the actual
    // access token.
    LOG(VERBOSE) << "Could not parse credential file as Service Account";
    return {};
  }
  static constexpr char BUILD_SCOPE[] =
      "https://www.googleapis.com/auth/androidbuild.internal";
  auto result = ServiceAccountOauthCredentialSource::FromJson(
      http_client, content, BUILD_SCOPE);
  if (!result.ok()) {
    LOG(VERBOSE) << "Failed to load service account json file: \n"
                 << result.error().Trace();
    return {};
  }
  return std::unique_ptr<CredentialSource>(
      new ServiceAccountOauthCredentialSource(std::move(*result)));
}

Result<std::vector<std::string>> ProcessHostPackage(
    BuildApi& build_api, const Build& build, const std::string& target_dir,
    const bool keep_archives) {
  std::string host_tools_filepath =
      CF_EXPECT(build_api.DownloadFile(build, target_dir, HOST_TOOLS));
  return ExtractArchiveContents(host_tools_filepath, target_dir, keep_archives);
}

Result<BuildApi> GetBuildApi(const BuildApiFlags& flags) {
  auto resolver =
      flags.external_dns_resolver ? GetEntDnsResolve : NameResolver();
  std::unique_ptr<HttpClient> curl = HttpClient::CurlClient(resolver);
  std::unique_ptr<HttpClient> retrying_http_client =
      HttpClient::ServerErrorRetryClient(*curl, 10,
                                         std::chrono::milliseconds(5000));
  std::unique_ptr<CredentialSource> credential_source;
  if (flags.credential_source == "gce") {
    credential_source =
        GceMetadataCredentialSource::make(*retrying_http_client);
  } else if (flags.credential_source == "") {
    std::string file = StringFromEnv("HOME", ".") + "/.acloud_oauth2.dat";
    LOG(VERBOSE) << "Probing acloud credentials at " << file;
    if (FileExists(file)) {
      std::ifstream stream(file);
      auto attempt_load =
          RefreshCredentialSource::FromOauth2ClientFile(*curl, stream);
      if (attempt_load.ok()) {
        credential_source.reset(
            new RefreshCredentialSource(std::move(*attempt_load)));
      } else {
        LOG(VERBOSE) << "Failed to load acloud credentials: "
                     << attempt_load.error().Trace();
      }
    } else {
      LOG(INFO) << "\"" << file << "\" missing, running without credentials";
    }
  } else if (!FileExists(flags.credential_source)) {
    // If the parameter doesn't point to an existing file it must be the
    // credentials.
    credential_source = FixedCredentialSource::make(flags.credential_source);
  } else {
    // Read the file only once in case it's a pipe.
    LOG(VERBOSE) << "Attempting to open credentials file \""
                 << flags.credential_source << "\"";
    auto file = SharedFD::Open(flags.credential_source, O_RDONLY);
    CF_EXPECT(file->IsOpen(),
              "Failed to open credential_source file: " << file->StrError());
    std::string file_content;
    auto size = ReadAll(file, &file_content);
    CF_EXPECT(size >= 0,
              "Failed to read credentials file: " << file->StrError());
    if (auto crds = TryParseServiceAccount(*curl, file_content)) {
      credential_source = std::move(crds);
    } else {
      credential_source = FixedCredentialSource::make(file_content);
    }
  }

  return BuildApi(std::move(retrying_http_client), std::move(curl),
                  std::move(credential_source), flags.api_key,
                  flags.wait_retry_period);
}

Result<std::optional<Build>> GetBuildHelper(BuildApi& build_api,
                                            const std::string& build_source,
                                            const std::string& build_target) {
  if (build_source == "") {
    return std::nullopt;
  }
  return CF_EXPECT(build_api.ArgumentToBuild(build_source, build_target),
                   "Unable to create build from source ("
                       << build_source << ") and target (" << build_target
                       << ")");
}

Result<Builds> GetBuildsFromSources(BuildApi& build_api,
                                    const BuildSourceFlags& build_sources) {
  std::optional<Build> default_build = CF_EXPECT(GetBuildHelper(
      build_api, build_sources.default_build, DEFAULT_BUILD_TARGET));
  CF_EXPECT(default_build.has_value());
  std::optional<Build> host_package = CF_EXPECT(GetBuildHelper(
      build_api, build_sources.host_package_build, DEFAULT_BUILD_TARGET));
  Builds result = Builds{
      .default_build = *default_build,
      .system = CF_EXPECT(GetBuildHelper(build_api, build_sources.system_build,
                                         DEFAULT_BUILD_TARGET)),
      .kernel = CF_EXPECT(
          GetBuildHelper(build_api, build_sources.kernel_build, KERNEL)),
      .boot = CF_EXPECT(GetBuildHelper(build_api, build_sources.boot_build,
                                       "gki_x86_64-user")),
      .bootloader = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.bootloader_build, "u-boot_crosvm_x86_64")),
      .otatools = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.otatools_build, DEFAULT_BUILD_TARGET)),
      .host_package = host_package.value_or(result.default_build),
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

Result<TargetDirectories> CreateDirectories(
    const std::string& target_directory) {
  TargetDirectories targets =
      TargetDirectories{.root = target_directory,
                        .otatools = target_directory + OTA_TOOLS_DIR,
                        .default_target_files = target_directory + DEFAULT_DIR,
                        .system_target_files = target_directory + SYSTEM_DIR};

  for (const auto& dir_path :
       {targets.root, targets.otatools, targets.default_target_files,
        targets.system_target_files}) {
    CF_EXPECT(EnsureDirectoryExists(dir_path, RWX_ALL_MODE));
  }
  return {targets};
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
    std::cout << target_directory << "/" << file.second.file_path << "\n";
  }
  std::cout << std::flush;
  return {};
}

Result<void> Fetch(BuildApi& build_api, const Builds& builds,
                   const TargetDirectories& target_directories,
                   const DownloadFlags& flags,
                   const bool keep_downloaded_archives,
                   const bool is_host_package_build, FetcherConfig& config) {
  auto process_pkg_ret = std::async(
      std::launch::async, ProcessHostPackage, std::ref(build_api),
      std::cref(builds.host_package), std::cref(target_directories.root),
      std::cref(keep_downloaded_archives));

  const auto [default_build_id, default_build_target] =
      GetBuildIdAndTarget(builds.default_build);

  // Some older builds might not have misc_info.txt, so permit errors on
  // fetching misc_info.txt
  Result<std::string> misc_info_result = build_api.DownloadFile(
      builds.default_build, target_directories.root, "misc_info.txt");
  if (misc_info_result.ok()) {
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::DEFAULT_BUILD, default_build_id, default_build_target,
        {misc_info_result.value()}, target_directories.root, OVERRIDE_ENTRIES));
  }

  if (flags.download_img_zip) {
    std::string img_zip_name = GetBuildZipName(builds.default_build, "img");
    std::string default_img_zip_filepath = CF_EXPECT(build_api.DownloadFile(
        builds.default_build, target_directories.root, img_zip_name));
    std::vector<std::string> image_files = CF_EXPECT(ExtractArchiveContents(
        default_img_zip_filepath, target_directories.root,
        keep_downloaded_archives));
    LOG(INFO) << "Adding img-zip files for default build";
    for (auto& file : image_files) {
      LOG(INFO) << file;
    }
    CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                      default_build_id, default_build_target,
                                      image_files, target_directories.root));
  }
  if (builds.system || flags.download_target_files_zip) {
    std::string target_files_name =
        GetBuildZipName(builds.default_build, "target_files");
    std::string target_files = CF_EXPECT(build_api.DownloadFile(
        builds.default_build, target_directories.default_target_files,
        target_files_name));
    LOG(INFO) << "Adding target files for default build";
    CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                      default_build_id, default_build_target,
                                      {target_files}, target_directories.root));
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
              OVERRIDE_ENTRIES));
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
    RenameFile(downloaded_kernel_filepath, kernel_filepath);
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
    std::string boot_filepath;
    if (flags.boot_artifact != "") {
      boot_filepath = CF_EXPECT(build_api.DownloadFileWithBackup(
          *builds.boot, target_directories.root, flags.boot_artifact,
          boot_img_zip_name));
    } else {
      boot_filepath = CF_EXPECT(build_api.DownloadFile(
          *builds.boot, target_directories.root, boot_img_zip_name));
    }

    std::vector<std::string> boot_files;
    // downloaded a zip that needs to be extracted
    if (android::base::EndsWith(boot_filepath, boot_img_zip_name)) {
      std::string extract_target =
          flags.boot_artifact != "" ? flags.boot_artifact : "boot.img";
      std::string extracted_boot = CF_EXPECT(
          ExtractImage(boot_filepath, target_directories.root, extract_target));
      std::string target_boot = CF_EXPECT(
          RenameFile(extracted_boot, target_directories.root + "/boot.img"));
      boot_files.push_back(target_boot);

      // keep_downloaded_archives flag used because this is the last extract
      // on this archive
      Result<std::string> extracted_vendor_boot_result =
          ExtractImage(boot_filepath, target_directories.root,
                       "vendor_boot.img", keep_downloaded_archives);
      if (extracted_vendor_boot_result.ok()) {
        boot_files.push_back(extracted_vendor_boot_result.value());
      }
    } else {
      boot_files.push_back(boot_filepath);
    }
    const auto [boot_id, boot_target] = GetBuildIdAndTarget(*builds.boot);
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::BOOT_BUILD, boot_id, boot_target, boot_files,
        target_directories.root, OVERRIDE_ENTRIES));
  }

  if (builds.bootloader) {
    std::string bootloader_filepath = target_directories.root + "/bootloader";
    // If the bootloader is from an arm/aarch64 build, the artifact will be of
    // filetype bin.
    std::string downloaded_bootloader_filepath =
        CF_EXPECT(build_api.DownloadFileWithBackup(*builds.bootloader,
                                                   target_directories.root,
                                                   "u-boot.rom", "u-boot.bin"));
    RenameFile(downloaded_bootloader_filepath, bootloader_filepath);
    const auto [bootloader_id, bootloader_target] =
        GetBuildIdAndTarget(*builds.bootloader);
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::BOOTLOADER_BUILD, bootloader_id, bootloader_target,
        {bootloader_filepath}, target_directories.root, OVERRIDE_ENTRIES));
  }

  if (builds.otatools) {
    std::string otatools_filepath = CF_EXPECT(build_api.DownloadFile(
        *builds.otatools, target_directories.root, OTA_TOOLS));
    std::vector<std::string> ota_tools_files = CF_EXPECT(
        ExtractArchiveContents(otatools_filepath, target_directories.otatools,
                               keep_downloaded_archives));
    const auto [otatools_build_id, otatools_build_target] =
        GetBuildIdAndTarget(*builds.otatools);
    CF_EXPECT(config.AddFilesToConfig(
        FileSource::DEFAULT_BUILD, otatools_build_id, otatools_build_target,
        ota_tools_files, target_directories.root));
  }

  // Wait for ProcessHostPackage to return.
  std::vector<std::string> host_package_files =
      CF_EXPECT(process_pkg_ret.get());
  const auto [host_id, host_target] = GetBuildIdAndTarget(builds.host_package);
  FileSource host_filesource = FileSource::DEFAULT_BUILD;
  if (is_host_package_build) {
    host_filesource = FileSource::HOST_PACKAGE_BUILD;
  }
  CF_EXPECT(config.AddFilesToConfig(host_filesource, host_id, host_target,
                                    host_package_files,
                                    target_directories.root));
  return {};
}

}  // namespace

Result<void> FetchCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  const FetchFlags flags = CF_EXPECT(GetFlagValues(argc, argv));

#ifdef __BIONIC__
  // TODO(schuffelen): Find a better way to deal with tzdata
  setenv("ANDROID_TZDATA_ROOT", "/", /* overwrite */ 0);
  setenv("ANDROID_ROOT", "/", /* overwrite */ 0);
#endif
  const std::string fetch_root_directory = AbsolutePath(flags.target_directory);
  const bool add_subdirectory =
      flags.build_target_flags.size() > 1 || !flags.target_subdirectory.empty();

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    BuildApi build_api = CF_EXPECT(GetBuildApi(flags.build_api_flags));

    for (const auto& [build_source_flags, download_flags, index] :
         flags.build_target_flags) {
      std::string build_directory = fetch_root_directory;
      if (add_subdirectory) {
        build_directory += "/" + AccessOrDefault<std::string>(
                                     flags.target_subdirectory, index,
                                     "build_" + std::to_string(index));
      }
      const TargetDirectories target_directories =
          CF_EXPECT(CreateDirectories(build_directory));
      FetcherConfig config;
      const Builds builds =
          CF_EXPECT(GetBuildsFromSources(build_api, build_source_flags));
      const bool is_host_package_build =
          build_source_flags.host_package_build != "";
      CF_EXPECT(Fetch(build_api, builds, target_directories, download_flags,
                      flags.keep_downloaded_archives, is_host_package_build,
                      config));
      CF_EXPECT(SaveConfig(config, target_directories.root));
    }
  }
  curl_global_cleanup();
  return {};
}

}  // namespace cuttlefish
