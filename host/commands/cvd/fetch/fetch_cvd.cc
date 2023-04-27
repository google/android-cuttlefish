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
#include "common/libs/utils/subprocess.h"
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

struct BuildSourceFlags {
  std::string default_build = DEFAULT_BRANCH + "/" + DEFAULT_BUILD_TARGET;
  std::string system_build = "";
  std::string kernel_build = "";
  std::string boot_build = "";
  std::string bootloader_build = "";
  std::string otatools_build = "";
  std::string host_package_build = "";
};

struct DownloadFlags {
  std::string boot_artifact = "";
  bool download_img_zip = true;
  bool download_target_files_zip = false;
};

struct FetchFlags {
  std::string target_directory = "";
  bool keep_downloaded_archives = false;
  bool helpxml = false;
  BuildApiFlags build_api_flags;
  BuildSourceFlags build_source_flags;
  DownloadFlags download_flags;
};

struct Builds {
  Build default_build;
  std::optional<Build> system;
  std::optional<Build> kernel;
  std::optional<Build> boot;
  std::optional<Build> bootloader;
  std::optional<Build> otatools;
  std::optional<Build> host_package;
};

struct TargetDirectories {
  std::string root;
  std::string otatools;
  std::string default_target_files;
  std::string system_target_files;
};

std::vector<Flag> GetFlagsVector(FetchFlags& fetch_flags,
                                 BuildApiFlags& build_api_flags,
                                 BuildSourceFlags& build_source_flags,
                                 DownloadFlags& download_flags,
                                 int& retry_period, std::string& directory) {
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
      GflagsCompatFlag("default_build", build_source_flags.default_build)
          .Help("source for the cuttlefish build to use (vendor.img + host)"));
  flags.emplace_back(
      GflagsCompatFlag("system_build", build_source_flags.system_build)
          .Help("source for system.img and product.img"));
  flags.emplace_back(
      GflagsCompatFlag("kernel_build", build_source_flags.kernel_build)
          .Help("source for the kernel or gki target"));
  flags.emplace_back(
      GflagsCompatFlag("boot_build", build_source_flags.boot_build)
          .Help("source for the boot or gki target"));
  flags.emplace_back(
      GflagsCompatFlag("bootloader_build", build_source_flags.bootloader_build)
          .Help("source for the bootloader target"));
  flags.emplace_back(
      GflagsCompatFlag("otatools_build", build_source_flags.otatools_build)
          .Help("source for the host ota tools"));
  flags.emplace_back(GflagsCompatFlag("host_package_build",
                                      build_source_flags.host_package_build)
                         .Help("source for the host cvd tools"));

  flags.emplace_back(
      GflagsCompatFlag("boot_artifact", download_flags.boot_artifact)
          .Help("name of the boot image in boot_build"));
  flags.emplace_back(
      GflagsCompatFlag("download_img_zip", download_flags.download_img_zip)
          .Help("Whether to fetch the -img-*.zip file."));
  flags.emplace_back(
      GflagsCompatFlag("download_target_files_zip",
                       download_flags.download_target_files_zip)
          .Help("Whether to fetch the -target_files-*.zip file."));

  flags.emplace_back(UnexpectedArgumentGuard());
  flags.emplace_back(HelpFlag(flags, USAGE_MESSAGE));
  flags.emplace_back(
      HelpXmlFlag(flags, std::cout, fetch_flags.helpxml, USAGE_MESSAGE));
  return flags;
}

Result<FetchFlags> GetFlagValues(int argc, char** argv) {
  FetchFlags fetch_flags;
  BuildApiFlags build_api_flags;
  BuildSourceFlags build_source_flags;
  DownloadFlags download_flags;
  int retry_period = DEFAULT_RETRY_PERIOD;
  std::string directory = "";

  std::vector<Flag> flags =
      GetFlagsVector(fetch_flags, build_api_flags, build_source_flags,
                     download_flags, retry_period, directory);
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
  fetch_flags.build_source_flags = build_source_flags;
  fetch_flags.download_flags = download_flags;
  return {fetch_flags};
}

std::unique_ptr<CredentialSource> TryOpenServiceAccountFile(
    HttpClient& http_client, const std::string& path) {
  LOG(VERBOSE) << "Attempting to open service account file \"" << path << "\"";
  Json::CharReaderBuilder builder;
  std::ifstream ifs(path);
  Json::Value content;
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, &content, &errorMessage)) {
    LOG(VERBOSE) << "Could not read config file \"" << path
                 << "\": " << errorMessage;
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
  } else if (auto crds = TryOpenServiceAccountFile(*curl, flags.credential_source)) {
    // It's a file, try reading it as a Service Account file first.
    credential_source = std::move(crds);
  } else {
    // If the file exists but is not a Service Account file then it must contain
    // the credentials.
    auto file = SharedFD::Open(flags.credential_source, O_RDONLY);
    CF_EXPECT(file->IsOpen(), "Failed to open credential file: " << file->StrError());
    std::string credentials;
    auto size = ReadAll(file, &credentials);
    CF_EXPECT(size >= 0, "Failed to read credentials file: " << file->StrError());
    credential_source = FixedCredentialSource::make(credentials);
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
  Builds result = Builds{
      .default_build = default_build.value(),
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
      .host_package = CF_EXPECT(GetBuildHelper(
          build_api, build_sources.host_package_build, DEFAULT_BUILD_TARGET)),
  };
  if (!result.otatools.has_value()) {
    if (result.system.has_value()) {
      result.otatools = result.system.value();
    } else if (result.kernel.has_value()) {
      result.otatools = result.default_build;
    }
  }
  if (!result.host_package.has_value()) {
    result.host_package = result.default_build;
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

}  // namespace

Result<void> FetchCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  const FetchFlags flags = CF_EXPECT(GetFlagValues(argc, argv));

#ifdef __BIONIC__
  // TODO(schuffelen): Find a better way to deal with tzdata
  setenv("ANDROID_TZDATA_ROOT", "/", /* overwrite */ 0);
  setenv("ANDROID_ROOT", "/", /* overwrite */ 0);
#endif

  std::string fetch_root_directory = AbsolutePath(flags.target_directory);
  const TargetDirectories target_directories =
      CF_EXPECT(CreateDirectories(fetch_root_directory));
  FetcherConfig config;
  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    BuildApi build_api = CF_EXPECT(GetBuildApi(flags.build_api_flags));
    const Builds builds =
        CF_EXPECT(GetBuildsFromSources(build_api, flags.build_source_flags));

    auto process_pkg_ret =
        std::async(std::launch::async, ProcessHostPackage, std::ref(build_api),
                   std::cref(builds.host_package.value()),
                   std::cref(target_directories.root),
                   std::cref(flags.keep_downloaded_archives));

    const auto [default_build_id, default_build_target] =
        GetBuildIdAndTarget(builds.default_build);
    if (builds.otatools.has_value()) {
      std::string otatools_filepath = CF_EXPECT(build_api.DownloadFile(
          builds.otatools.value(), target_directories.root, OTA_TOOLS));
      std::vector<std::string> ota_tools_files = CF_EXPECT(
          ExtractArchiveContents(otatools_filepath, target_directories.otatools,
                                 flags.keep_downloaded_archives));
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::DEFAULT_BUILD, default_build_id, default_build_target,
          ota_tools_files, target_directories.root));
    }
    if (flags.download_flags.download_img_zip) {
      std::string img_zip_name = GetBuildZipName(builds.default_build, "img");
      std::string default_img_zip_filepath = CF_EXPECT(build_api.DownloadFile(
          builds.default_build, target_directories.root, img_zip_name));
      std::vector<std::string> image_files = CF_EXPECT(ExtractArchiveContents(
          default_img_zip_filepath, target_directories.root,
          flags.keep_downloaded_archives));
      LOG(INFO) << "Adding img-zip files for default build";
      for (auto& file : image_files) {
        LOG(INFO) << file;
      }
      CF_EXPECT(config.AddFilesToConfig(FileSource::DEFAULT_BUILD,
                                        default_build_id, default_build_target,
                                        image_files, target_directories.root));
    }
    if (builds.system.has_value() ||
        flags.download_flags.download_target_files_zip) {
      std::string target_files_name =
          GetBuildZipName(builds.default_build, "target_files");
      std::string target_files = CF_EXPECT(build_api.DownloadFile(
          builds.default_build, target_directories.default_target_files,
          target_files_name));
      LOG(INFO) << "Adding target files for default build";
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::DEFAULT_BUILD, default_build_id, default_build_target,
          {target_files}, target_directories.root));
    }

    if (builds.system.has_value()) {
      std::string target_files_name =
          GetBuildZipName(builds.system.value(), "target_files");
      std::string target_files = CF_EXPECT(build_api.DownloadFile(
          builds.system.value(), target_directories.system_target_files,
          target_files_name));
      const auto [system_id, system_target] =
          GetBuildIdAndTarget(builds.system.value());
      CF_EXPECT(config.AddFilesToConfig(FileSource::SYSTEM_BUILD, system_id,
                                        system_target, {target_files},
                                        target_directories.root));

      if (flags.download_flags.download_img_zip) {
        std::string system_img_zip_name =
            GetBuildZipName(builds.system.value(), "img");
        Result<std::string> system_img_zip_result = build_api.DownloadFile(
            builds.system.value(), target_directories.root,
            system_img_zip_name);
        Result<std::vector<std::string>> extract_result;
        if (system_img_zip_result.ok()) {
          extract_result = ExtractImages(
              system_img_zip_result.value(), target_directories.root,
              {"system.img", "product.img"}, flags.keep_downloaded_archives);
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

          Result<std::string> extracted_vbmeta_system =
              ExtractImage(target_files, target_directories.root,
                           "IMAGES/vbmeta_system.img");
          if (extracted_vbmeta_system.ok()) {
            CF_EXPECT(
                RenameFile(extracted_vbmeta_system.value(),
                           target_directories.root + "/vbmeta_system.img"));
          }
        }
      }
    }

    if (builds.kernel.has_value()) {
      std::string kernel_filepath = target_directories.root + "/kernel";
      // If the kernel is from an arm/aarch64 build, the artifact will be called
      // Image.
      std::string downloaded_kernel_filepath =
          CF_EXPECT(build_api.DownloadFileWithBackup(builds.kernel.value(),
                                                     target_directories.root,
                                                     "bzImage", "Image"));
      RenameFile(downloaded_kernel_filepath, kernel_filepath);
      const auto [kernel_id, kernel_target] =
          GetBuildIdAndTarget(builds.kernel.value());
      CF_EXPECT(config.AddFilesToConfig(FileSource::KERNEL_BUILD, kernel_id,
                                        kernel_target, {kernel_filepath},
                                        target_directories.root));

      // Certain kernel builds do not have corresponding ramdisks.
      Result<std::string> initramfs_img_result = build_api.DownloadFile(
          builds.kernel.value(), target_directories.root, "initramfs.img");
      if (initramfs_img_result.ok()) {
        CF_EXPECT(config.AddFilesToConfig(
            FileSource::KERNEL_BUILD, kernel_id, kernel_target,
            {initramfs_img_result.value()}, target_directories.root));
      }
    }

    if (builds.boot.has_value()) {
      std::string boot_img_zip_name =
          GetBuildZipName(builds.boot.value(), "img");
      std::string boot_filepath;
      if (flags.download_flags.boot_artifact != "") {
        boot_filepath = CF_EXPECT(build_api.DownloadFileWithBackup(
            builds.boot.value(), target_directories.root,
            flags.download_flags.boot_artifact, boot_img_zip_name));
      } else {
        boot_filepath = CF_EXPECT(build_api.DownloadFile(
            builds.boot.value(), target_directories.root, boot_img_zip_name));
      }

      std::vector<std::string> boot_files;
      // downloaded a zip that needs to be extracted
      if (android::base::EndsWith(boot_filepath, boot_img_zip_name)) {
        std::string extract_target = flags.download_flags.boot_artifact != ""
                                         ? flags.download_flags.boot_artifact
                                         : "boot.img";
        std::string extracted_boot = CF_EXPECT(ExtractImage(
            boot_filepath, target_directories.root, extract_target));
        std::string target_boot = CF_EXPECT(
            RenameFile(extracted_boot, target_directories.root + "/boot.img"));
        boot_files.push_back(target_boot);

        // keep_downloaded_archives flag used because this is the last extract
        // on this archive
        Result<std::string> extracted_vendor_boot_result =
            ExtractImage(boot_filepath, target_directories.root,
                         "vendor_boot.img", flags.keep_downloaded_archives);
        if (extracted_vendor_boot_result.ok()) {
          boot_files.push_back(extracted_vendor_boot_result.value());
        }
      } else {
        boot_files.push_back(boot_filepath);
      }
      const auto [boot_id, boot_target] =
          GetBuildIdAndTarget(builds.boot.value());
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::BOOT_BUILD, boot_id, boot_target, boot_files,
          target_directories.root, OVERRIDE_ENTRIES));
    }

    // Some older builds might not have misc_info.txt, so permit errors on
    // fetching misc_info.txt
    Result<std::string> misc_info_result = build_api.DownloadFile(
        builds.default_build, target_directories.root, "misc_info.txt");
    if (misc_info_result.ok()) {
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::DEFAULT_BUILD, default_build_id, default_build_target,
          {misc_info_result.value()}, target_directories.root,
          OVERRIDE_ENTRIES));
    }

    if (builds.bootloader.has_value()) {
      std::string bootloader_filepath = target_directories.root + "/bootloader";
      // If the bootloader is from an arm/aarch64 build, the artifact will be of
      // filetype bin.
      std::string downloaded_bootloader_filepath =
          CF_EXPECT(build_api.DownloadFileWithBackup(
              builds.bootloader.value(), target_directories.root, "u-boot.rom",
              "u-boot.bin"));
      RenameFile(downloaded_bootloader_filepath, bootloader_filepath);
      const auto [bootloader_id, bootloader_target] =
          GetBuildIdAndTarget(builds.bootloader.value());
      CF_EXPECT(config.AddFilesToConfig(
          FileSource::BOOTLOADER_BUILD, bootloader_id, bootloader_target,
          {bootloader_filepath}, target_directories.root, OVERRIDE_ENTRIES));
    }

    // Wait for ProcessHostPackage to return.
    std::vector<std::string> host_package_files =
        CF_EXPECT(process_pkg_ret.get());
    FileSource host_filesource = FileSource::DEFAULT_BUILD;
    std::string host_id = default_build_id;
    std::string host_target = default_build_target;
    if (flags.build_source_flags.host_package_build != "") {
      host_filesource = FileSource::HOST_PACKAGE_BUILD;
      const auto [id, target] =
          GetBuildIdAndTarget(builds.host_package.value());
      host_id = id;
      host_target = target;
    }
    CF_EXPECT(config.AddFilesToConfig(host_filesource, host_id, host_target,
                                      host_package_files,
                                      target_directories.root));
  }
  curl_global_cleanup();
  CF_EXPECT(SaveConfig(config, target_directories.root));
  return {};
}

}  // namespace cuttlefish
